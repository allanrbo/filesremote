// Copyright 2020 Allan Riordan Boll

#include "src/sftpconnection.h"

#ifdef __WXMSW__

#include <winsock2.h>

#else

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#endif

#include <wx/secretstore.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <future>  // NOLINT
#include <optional>
#include <regex>  // NOLINT
#include <sstream>
#include <string>
#include <vector>

#include "./version.h"
#include "src/direntry.h"
#include "src/hostdesc.h"
#include "src/string.h"

using std::exception;
using std::function;
using std::nullopt;
using std::optional;
using std::regex;
using std::regex_replace;
using std::regex_search;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;

#define BUFLEN 4096

// RAII wrapper to ensure LIBSSH2_SFTP_HANDLE gets closed.
class SftpHandle {
public:
    LIBSSH2_SFTP_HANDLE *handle_;

    explicit SftpHandle(LIBSSH2_SFTP_HANDLE *handle) : handle_(handle) {}

    ~SftpHandle() {
        if (this->handle_) {
            libssh2_sftp_close(this->handle_);
        }
    }
};

// RAII wrapper to ensure LIBSSH2_CHANNEL gets closed.
class ChannelHandle {
public:
    LIBSSH2_CHANNEL *channel_ = NULL;

    explicit ChannelHandle(LIBSSH2_CHANNEL *channel) : channel_(channel) {}

    ~ChannelHandle() {
        if (this->channel_) {
            libssh2_channel_close(this->channel_);
            libssh2_channel_free(this->channel_);
        }
    }
};

// RAII wrapper to ensure FILE gets closed.
class FileHandle {
public:
    FILE *handle_;

    explicit FileHandle(FILE *handle) : handle_(handle) {}

    ~FileHandle() {
        if (this->handle_) {
            fclose(this->handle_);
        }
    }
};

SftpConnection::SftpConnection(HostDesc host_desc) {
    this->host_desc_ = host_desc;

    int rc;

#ifdef __WXMSW__
    WSADATA wsadata;
    rc = WSAStartup(MAKEWORD(2, 0), &wsadata);
    if (rc != 0) {
        throw ConnectionError("WSAStartup failed (" + to_string(rc) + ")");
    }
#endif

    rc = libssh2_init(-1);
    if (rc != 0) {
        throw ConnectionError("libssh2_init failed. " + this->GetLastErrorMsg());
    }

    this->sock_ = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(this->host_desc_.port_);
    sin.sin_addr.s_addr = inet_addr(this->host_desc_.host_.c_str());
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *remote_host = gethostbyname(this->host_desc_.host_.c_str());
        if (!remote_host) {
            throw ConnectionError(
                    "failed to resolve hostname " + this->host_desc_.host_ + " (gethostbyname failed)");
        }

        sin.sin_addr.s_addr = *reinterpret_cast<u_long *>(remote_host->h_addr_list[0]);
    }

    if (connect(this->sock_, (struct sockaddr *) (&sin), sizeof(struct sockaddr_in)) != 0) {
        throw ConnectionError(
                "socket connect failed on " + this->host_desc_.host_ + ":" + to_string(this->host_desc_.port_));
    }

    this->session_ = libssh2_session_init();
    if (!this->session_) {
        throw ConnectionError("libssh2_session_init failed. " + this->GetLastErrorMsg());
    }

    libssh2_session_set_blocking(this->session_, 1);
    libssh2_session_set_timeout(this->session_, 10 * 1000);  // TODO(allan): higher timeout?
    libssh2_session_banner_set(this->session_, "SSH-2.0-FilesRemote_" PROJECT_VERSION);

    rc = libssh2_session_handshake(this->session_, this->sock_);
    if (rc) {
        throw ConnectionError("libssh2_session_handshake failed. " + this->GetLastErrorMsg());
    }

    int hostkey_algos[3]{LIBSSH2_HOSTKEY_HASH_SHA256, LIBSSH2_HOSTKEY_HASH_SHA1, LIBSSH2_HOSTKEY_HASH_MD5};
    string hostkey_algo_names[3]{"SHA256", "SHA1", "MD5"};
    int hostkey_algo_keylen[3]{32, 20, 16};
    for (int i = 0; i < 3; ++i) {
        const char *fingerprint = libssh2_hostkey_hash(this->session_, hostkey_algos[i]);
        if (fingerprint == NULL) {
            continue;
        }

        string b = encodeBase64((unsigned char *) fingerprint, hostkey_algo_keylen[i]);
        // Trim off any padding chars, as the OpenSSH client usually does not show these either.
        while (!b.empty() && b[b.size() - 1] == '=') {
            b = b.substr(0, b.size() - 1);
        }

        this->fingerprint_ = hostkey_algo_names[i] + ":" + b;
        break;
    }
}

SftpConnection::~SftpConnection() {
    this->SudoExit();

    if (this->sftp_session_) {
        libssh2_sftp_shutdown(this->sftp_session_);
    }

    if (this->session_) {
        libssh2_session_disconnect(this->session_, "normal shutdown");
        libssh2_session_free(this->session_);
    }

    if (this->sock_) {
#ifdef __WXMSW__
        closesocket(this->sock_);
#else
        close(this->sock_);
#endif
    }

    libssh2_exit();
}

vector<DirEntry> SftpConnection::GetDir(string path) {
    int rc;

    auto sftp_handle_ = SftpHandle(libssh2_sftp_opendir(this->sftp_session_, path.c_str()));
    if (!sftp_handle_.handle_) {
        if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
            uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
            if (err == LIBSSH2_FX_PERMISSION_DENIED) {
                throw DirListFailedPermission(path);
            }

            if (err == LIBSSH2_FX_NO_SUCH_PATH || err == LIBSSH2_FX_NO_SUCH_FILE || err == LIBSSH2_FX_NO_MEDIA) {
                throw FileNotFound(path);
            }
        }

        throw ConnectionError("libssh2_sftp_opendir failed. " + this->GetLastErrorMsg());
    }

    auto files = vector<DirEntry>();
    while (1) {
        LIBSSH2_SFTP_ATTRIBUTES attrs;
        char name[BUFLEN];
        char line[BUFLEN];
        memset(name, 0, BUFLEN);
        memset(line, 0, BUFLEN);

        rc = libssh2_sftp_readdir_ex(sftp_handle_.handle_, name, sizeof(name), line, sizeof(line), &attrs);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            continue;
        }
        if (rc == 0) {
            break;
        }
        if (rc < 0) {
            throw ConnectionError("libssh2_sftp_readdir_ex failed. " + this->GetLastErrorMsg());
        }

        auto d = DirEntry(attrs);

        d.name_ = string(name);
        if (d.name_ == ".") {
            continue;
        }

        // Extract user, group and mode string from the free text line.
        stringstream s(line);
        string segment;
        vector<string> parts;
        int field_num = 0;
        while (getline(s, segment, ' ')) {
            if (segment.empty()) {
                continue;
            }

            if (field_num == 0) {
                if (segment.length() != 10) {
                    // Free text line was in an unexpected format.
                    break;
                }
                d.mode_str_ = string(segment);
            }

            if (field_num == 2) {
                d.owner_ = string(segment);
            }

            if (field_num == 3) {
                d.group_ = string(segment);
            }

            field_num++;
        }

        files.push_back(d);
    }

    if (files.size() == 0) {
        throw DirListFailedPermission(path);
    }

    return files;
}

bool SftpConnection::DownloadFile(string remote_src_path, string local_dst_path, function<bool(void)> cancelled) {
    auto sftp_handle_ = SftpHandle(libssh2_sftp_open(
                                           this->sftp_session_,
                                           remote_src_path.c_str(),
                                           LIBSSH2_FXF_READ,
                                           0));
    if (!sftp_handle_.handle_) {
        if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
            uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
            if (err == LIBSSH2_FX_PERMISSION_DENIED || err == LIBSSH2_FX_WRITE_PROTECT) {
                throw DownloadFailedPermission(remote_src_path);
            }
            throw DownloadFailed(remote_src_path);
        }
        throw ConnectionError(this->GetLastErrorMsg());
    }

#ifdef __WXMSW__
    auto local_file_handle_ = FileHandle(_wfopen(localPathUnicode(local_dst_path).c_str(), L"wb"));
#else
    auto local_file_handle_ = FileHandle(fopen(local_dst_path.c_str(), "wb"));
#endif
    // TODO(allan): error handling for fopen.

    char buf[BUFLEN];
    while (1) {
        if (cancelled()) {
            return false;
        }
        int rc = libssh2_sftp_read(sftp_handle_.handle_, buf, BUFLEN);
        if (rc > 0) {
            fwrite(buf, 1, rc, local_file_handle_.handle_);
            // TODO(allan): error handling for fwrite.
        } else if (rc == 0) {
            break;
        } else {
            if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
                throw DownloadFailed(remote_src_path);
            }
            throw ConnectionError("libssh2_sftp_read failed. " + this->GetLastErrorMsg());
        }
    }

    return true;
}

bool SftpConnection::UploadFile(string local_src_path, string remote_dst_path, function<bool(void)> cancelled) {
    int mode = LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH;
    auto sftp_openfile_handle_ = SftpHandle(libssh2_sftp_open(
                                                    this->sftp_session_,
                                                    remote_dst_path.c_str(),
                                                    LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC | LIBSSH2_FXF_CREAT,
                                                    mode));
    if (!sftp_openfile_handle_.handle_) {
        if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
            uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
            if (err == LIBSSH2_FX_PERMISSION_DENIED || err == LIBSSH2_FX_WRITE_PROTECT) {
                throw FailedPermission(remote_dst_path);
            }
            if (err == LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM) {
                throw UploadFailedSpace(remote_dst_path);
            }
            throw UploadFailed(remote_dst_path);
        }
        throw ConnectionError(this->GetLastErrorMsg());
    }

#ifdef __WXMSW__
    auto local_file_handle_ = FileHandle(_wfopen(localPathUnicode(local_src_path).c_str(), L"rb"));
#else
    auto local_file_handle_ = FileHandle(fopen(local_src_path.c_str(), "rb"));
#endif
    // TODO(allan): error handling for fopen.

    char buf[BUFLEN];
    while (1) {
        if (cancelled()) {
            return false;
        }
        int rc = fread(buf, 1, BUFLEN, local_file_handle_.handle_);
        if (rc > 0) {
            int nread = rc;
            char *p = buf;
            while (nread) {
                rc = libssh2_sftp_write(sftp_openfile_handle_.handle_, buf, rc);
                if (rc < 0) {
                    if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
                        uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
                        if (err == LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM) {
                            throw UploadFailedSpace(remote_dst_path);
                        }
                        throw UploadFailed(remote_dst_path);
                    }

                    throw ConnectionError("libssh2_sftp_write failed. " + this->GetLastErrorMsg());
                }
                p += rc;
                nread -= rc;
            }
        } else {
            // TODO(allan): error handling for fread.
            break;
        }
    }

    return true;
}

optional<DirEntry> SftpConnection::Stat(string remote_path) {
    auto sftp_handle_ = SftpHandle(libssh2_sftp_open(
                                           this->sftp_session_,
                                           remote_path.c_str(),
                                           0,
                                           0));

    if (!sftp_handle_.handle_) {
        if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
            uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
            if (err == LIBSSH2_FX_PERMISSION_DENIED || err == LIBSSH2_FX_WRITE_PROTECT) {
                throw FailedPermission(remote_path);
            }
            return nullopt;
        }
        throw ConnectionError(this->GetLastErrorMsg());
    }

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    if (libssh2_sftp_fstat(sftp_handle_.handle_, &attrs) != 0) {
        throw ConnectionError(this->GetLastErrorMsg());
    }

    DirEntry entry(attrs);
    return entry;
}

void SftpConnection::Rename(string remote_old_path, string remote_new_path) {
    int rc = libssh2_sftp_rename(this->sftp_session_, remote_old_path.c_str(), remote_new_path.c_str());
    if (rc != 0) {
        if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
            uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
            if (err == LIBSSH2_FX_PERMISSION_DENIED || err == LIBSSH2_FX_WRITE_PROTECT) {
                throw FailedPermission(remote_old_path.c_str());  // TODO(allan): different exceptions?
            }
            throw UploadFailed(remote_old_path.c_str());
        }
        throw ConnectionError(this->GetLastErrorMsg());
    }
}

void SftpConnection::Delete(string remote_path) {
    int rc;

    auto entry = this->Stat(remote_path);
    if (entry.has_value() && !entry->is_dir_) {
        rc = libssh2_sftp_unlink(this->sftp_session_, remote_path.c_str());
        if (rc != 0) {
            if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
                uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
                if (err == LIBSSH2_FX_PERMISSION_DENIED || err == LIBSSH2_FX_WRITE_PROTECT) {
                    throw FailedPermission(remote_path.c_str());
                }
            }
            throw DeleteFailed(remote_path, this->GetLastErrorMsg());
        }
    } else if (entry.has_value() && entry->is_dir_) {
        ChannelHandle channel(libssh2_channel_open_session(this->session_));
        if (!channel.channel_) {
            throw ConnectionError("libssh2_channel_open_session failed. " + this->GetLastErrorMsg());
        }

        remote_path = regex_replace(remote_path, regex("\""), "\\\"");

        if (this->sudo_channel_) {
            // -p is the same as --prompt, but the long version doesn't work on for example Debian 6.
            // -S is the same as --stdin, but the long version doesn't work on for example Debian 6.
            rc = libssh2_channel_exec(channel.channel_,
                                      ("sudo -p password: -S rm -fr \"" + remote_path + "\"").c_str());
            if (rc != 0) {
                throw ConnectionError("libssh2_channel_exec failed. " + this->GetLastErrorMsg());
            }

            if (this->sudo_passwd_.IsOk()) {
                this->SendSudoPasswd(channel.channel_);
            }
        } else {
            rc = libssh2_channel_exec(channel.channel_, ("rm -fr \"" + remote_path + "\"").c_str());
            if (rc != 0) {
                throw ConnectionError("libssh2_channel_exec failed. " + this->GetLastErrorMsg());
            }
        }

        char buf[BUFLEN];
        string output = "";
        while (1) {
            int n = libssh2_channel_read_stderr(channel.channel_, buf, BUFLEN);
            if (n == 0) {
                break;
            }
            output += string(buf, n);
        }

        libssh2_channel_close(channel.channel_);
        int status = libssh2_channel_get_exit_status(channel.channel_);
        if (status != 0) {
            throw DeleteFailed(remote_path, output);
        }
    } else {
        throw DeleteFailed(remote_path, "File not found.");
    }
}

void SftpConnection::Mkdir(string remote_path) {
    int mode = LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP | LIBSSH2_SFTP_S_IROTH |
               LIBSSH2_SFTP_S_IXOTH;
    int rc = libssh2_sftp_mkdir(this->sftp_session_, remote_path.c_str(), mode);
    if (rc != 0) {
        if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
            uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
            if (err == LIBSSH2_FX_PERMISSION_DENIED || err == LIBSSH2_FX_WRITE_PROTECT) {
                throw FailedPermission(remote_path.c_str());  // TODO(allan): different exceptions?
            }
            throw UploadFailed(remote_path.c_str());
        }
        throw ConnectionError(this->GetLastErrorMsg());
    }
}

void SftpConnection::Mkfile(string remote_path) {
    int mode = LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH;
    auto sftp_openfile_handle_ = SftpHandle(
            libssh2_sftp_open(
                    this->sftp_session_,
                    remote_path.c_str(),
                    LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL,
                    mode));
    if (!sftp_openfile_handle_.handle_) {
        if (libssh2_session_last_errno(this->session_) == LIBSSH2_ERROR_SFTP_PROTOCOL) {
            uint64_t err = libssh2_sftp_last_error(this->sftp_session_);
            if (err == LIBSSH2_FX_PERMISSION_DENIED || err == LIBSSH2_FX_WRITE_PROTECT) {
                throw FailedPermission(remote_path);
            }
            if (err == LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM) {
                throw UploadFailedSpace(remote_path);
            }
            auto s = this->GetLastErrorMsg();
            throw UploadFailed(remote_path);
        }
        throw ConnectionError(this->GetLastErrorMsg());
    }

    return;
}

string SftpConnection::RealPath(string remote_path) {
    char buf[BUFLEN];
    int rc = libssh2_sftp_realpath(this->sftp_session_, remote_path.c_str(), buf, BUFLEN);
    if (rc < 0) {
        throw ConnectionError("libssh2_sftp_realpath failed. " + this->GetLastErrorMsg());
    }
    return string(buf, rc);
}

static char *kbd_callback_passwd = NULL;
static int kbd_callback_passwd_len = 0;

static void kbd_callback(
        const char *name,
        int name_len,
        const char *instruction,
        int instruction_len,
        int num_prompts,
        const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
        LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
        void **abstract) {
    (void) abstract;

    if (num_prompts != 1) {
        return;
    }

    responses[0].text = kbd_callback_passwd;
    responses[0].length = kbd_callback_passwd_len;
}

bool SftpConnection::PasswordAuth(wxSecretValue passwd) {
    char *userauthlist = libssh2_userauth_list(
            this->session_,
            this->host_desc_.username_.c_str(),
            this->host_desc_.username_.length());
    if (!userauthlist) {
        throw ConnectionError("no authentication options");
    }

    auto p = reinterpret_cast<const char *>(passwd.GetData());

    if (regex_search(userauthlist, regex("(^|,)password($|,)"))) {
        char *s = reinterpret_cast<char *>(malloc(passwd.GetSize() + 1));
        memcpy(s, p, passwd.GetSize());
        s[passwd.GetSize()] = 0;  // Null terminate.
        int rc = libssh2_userauth_password(this->session_, this->host_desc_.username_.c_str(), s);
        wxSecretValue::Wipe(passwd.GetSize() + 1, s);
        free(s);
        if (rc) {
            return false;
        }
    } else if (regex_search(userauthlist, regex("(^|,)keyboard-interactive($|,)"))) {
        // libssh2 documentation says it will free this memory.
        kbd_callback_passwd = reinterpret_cast<char *>(malloc(passwd.GetSize()));
        memcpy(kbd_callback_passwd, p, passwd.GetSize());
        kbd_callback_passwd_len = passwd.GetSize();
        int rc = libssh2_userauth_keyboard_interactive(
                this->session_,
                this->host_desc_.username_.c_str(),
                &kbd_callback);
        kbd_callback_passwd = NULL;
        kbd_callback_passwd_len = 0;
        if (rc) {
            return false;
        }
    } else {
        return false;
    }

    this->SftpSubsystemInit();
    return true;
}

bool SftpConnection::AgentAuth() {
    // TODO(allan): check first if agent auth is even supported...
    //// char *userauthlist = libssh2_userauth_list(this->session, username.c_str(), username.size());

    LIBSSH2_AGENT *agent = libssh2_agent_init(this->session_);
    if (!agent) {
        return false;
    }

    if (libssh2_agent_connect(agent)) {
        return false;
    }

    if (libssh2_agent_list_identities(agent)) {
        return false;
    }

    struct libssh2_agent_publickey *identity, *prev_identity = NULL;
    while (1) {
        int rc = libssh2_agent_get_identity(agent, &identity, prev_identity);
        if (rc != 0) {
            return false;
        }

        if (libssh2_agent_userauth(agent, this->host_desc_.username_.c_str(), identity) == 0) {
            this->SftpSubsystemInit();
            return true;
        }

        prev_identity = identity;
    }
}

void SftpConnection::SendKeepAlive() {
    // The actual libssh2_keepalive_send doesn't really seem to work, so doing this instead as a workaround.
    this->RealPath(".");
}

void SftpConnection::SftpSubsystemInit() {
    this->sftp_session_ = libssh2_sftp_init(this->session_);
    if (!this->sftp_session_) {
        throw ConnectionError("libssh2_sftp_init failed. " + this->GetLastErrorMsg());
    }

    this->home_dir_ = this->RealPath(".");
}

static void _htonu32(char *buf, uint32_t value) {
    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;
}

void SftpConnection::SudoEnter() {
    int rc;

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(this->session_);
    if (!channel) {
        throw ConnectionError("libssh2_channel_open_session failed. " + this->GetLastErrorMsg());
    }

    auto sftp_server_paths = vector<string>{
            "/usr/lib/sftp-server",
            "/usr/lib/ssh/sftp-server",
            "/usr/lib/openssh/sftp-server",
            "/usr/libexec/sftp-server",
            "/usr/libexec/ssh/sftp-server",
            "/usr/libexec/openssh/sftp-server"
    };
    string sftp_server_path;
    for (int i = 0; i < sftp_server_paths.size(); ++i) {
        if (this->Stat(sftp_server_paths[i]).has_value()) {
            sftp_server_path = sftp_server_paths[i];
            break;
        }
    }
    if (sftp_server_path.empty()) {
        throw SudoFailed("Could not find location of sftp-server for sudo.");
    }

    // -p is the same as --prompt, but the long version doesn't work on for example Debian 6.
    // -S is the same as --stdin, but the long version doesn't work on for example Debian 6.
    rc = libssh2_channel_exec(channel, ("sudo -p password: -S " + sftp_server_path).c_str());
    if (rc != 0) {
        string msg = "libssh2_channel_exec failed while starting sudo ";
        msg += sftp_server_path;
        msg += ". ";
        msg += this->GetLastErrorMsg();
        throw ConnectionError(msg);
    }

    char buf[BUFLEN];

    if (this->sudo_passwd_.IsOk()) {
        this->SendSudoPasswd(channel);
    }

    // Send SSH_FXP_INIT command.
    memset(buf, 0, BUFLEN);
    _htonu32(buf, 5);  // Cmd length, excluding the length field itself.
    buf[4] = 1;  // SSH_FXP_INIT
    _htonu32(buf + 5, LIBSSH2_SFTP_VERSION);
    rc = libssh2_channel_write(channel, buf, 9);
    if (rc != 9) {
        throw SudoFailed("Error while sending SSH_FXP_INIT while establishing sudo sftp-server channel.");
    }

    memset(buf, 0, BUFLEN);
    int n = libssh2_channel_read(channel, buf, BUFLEN);
    if (n == 0) {
        string msg = "Unexpected output after sending SSH_FXP_INIT while establishing sudo sftp-server channel.";
        throw SudoFailed(msg);
    }

    // Keep ref to old non-sudo SFTP channel so we can use it again later if we exit sudo.
    this->non_sudo_channel_ = libssh2_sftp_get_channel(this->sftp_session_);
    this->sudo_channel_ = channel;

    // Replace with new channel.
    void **pp = reinterpret_cast<void **>(this->sftp_session_);  // First member of struct LIBSSH2_SFTP is channel.
    *pp = reinterpret_cast<void *>(channel);
}

void SftpConnection::SudoExit() {
    if (this->sudo_channel_ == NULL) {
        return;
    }

    // Replace with orig channel.
    void **pp = reinterpret_cast<void **>(this->sftp_session_);  // First member of struct LIBSSH2_SFTP is channel.
    *pp = reinterpret_cast<void *>(this->non_sudo_channel_);

    libssh2_channel_close(this->sudo_channel_);
    libssh2_channel_free(this->sudo_channel_);
    this->sudo_channel_ = NULL;
    this->non_sudo_channel_ = NULL;
}

bool SftpConnection::CheckSudoInstalled() {
    ChannelHandle channel(libssh2_channel_open_session(this->session_));
    if (!channel.channel_) {
        throw ConnectionError("libssh2_channel_open_session failed. " + this->GetLastErrorMsg());
    }

    int rc = libssh2_channel_exec(channel.channel_, "which sudo");
    if (rc != 0) {
        throw ConnectionError("libssh2_channel_exec failed. " + this->GetLastErrorMsg());
    }

    libssh2_channel_wait_eof(channel.channel_);
    libssh2_channel_close(channel.channel_);
    int status = libssh2_channel_get_exit_status(channel.channel_);
    if (status == 0) {
        return true;
    }

    return false;
}

bool SftpConnection::CheckSudoNeedsPasswd() {
    ChannelHandle channel(libssh2_channel_open_session(this->session_));
    if (!channel.channel_) {
        throw ConnectionError("libssh2_channel_open_session failed. " + this->GetLastErrorMsg());
    }

    // -n is the same as --non-interactive, but the long version doesn't work on for example Debian 6.
    int rc = libssh2_channel_exec(channel.channel_, "sudo -n true");
    if (rc != 0) {
        throw ConnectionError("libssh2_channel_exec failed. " + this->GetLastErrorMsg());
    }

    libssh2_channel_wait_eof(channel.channel_);
    libssh2_channel_close(channel.channel_);
    int status = libssh2_channel_get_exit_status(channel.channel_);
    if (status != 0) {
        // The sudo command failed, so it probably needed a password.
        return true;
    }

    return false;
}

bool SftpConnection::CheckSudoPasswd() {
    ChannelHandle channel(libssh2_channel_open_session(this->session_));
    if (!channel.channel_) {
        throw ConnectionError("libssh2_channel_open_session failed. " + this->GetLastErrorMsg());
    }

    // -p is the same as --prompt, but the long version doesn't work on for example Debian 6.
    // -S is the same as --stdin, but the long version doesn't work on for example Debian 6.
    int rc = libssh2_channel_exec(channel.channel_, "sudo -p password: -S true");
    if (rc != 0) {
        throw ConnectionError("libssh2_channel_exec failed. " + this->GetLastErrorMsg());
    }

    this->SendSudoPasswd(channel.channel_);

    char buf[BUFLEN];
    int n = libssh2_channel_read_stderr(channel.channel_, buf, BUFLEN);
    if (n != 0) {
        auto msg = string(buf, n);
        msg = regex_replace(msg, regex("\npassword:"), "");
        msg = "Output from sudo command:\n" + msg;
        throw SudoFailed(msg);
    }

    libssh2_channel_close(channel.channel_);
    int status = libssh2_channel_get_exit_status(channel.channel_);
    if (status != 0) {
        // The sudo command failed, so probably the password was wrong or we did not have sudo permissions.
        return false;
    }

    return true;
}

void SftpConnection::SendSudoPasswd(LIBSSH2_CHANNEL *channel) {
    char buf[BUFLEN];

    memset(buf, 0, BUFLEN);
    int n = libssh2_channel_read_stderr(channel, buf, BUFLEN);
    if (n == 0) {
        throw SudoFailed("Failed to launch sudo.");
    }

    if (strcmp(buf, "password:") != 0) {
        throw SudoFailed("Sudo did not show expected password prompt.");
    }

    int len = this->sudo_passwd_.GetSize() + 1;
    auto p = reinterpret_cast<const char *>(this->sudo_passwd_.GetData());
    char *s = reinterpret_cast<char *>(malloc(len));
    memcpy(s, p, this->sudo_passwd_.GetSize());
    s[this->sudo_passwd_.GetSize()] = '\n';
    int rc = libssh2_channel_write(channel, s, len);
    wxSecretValue::Wipe(this->sudo_passwd_.GetSize() + 1, s);
    free(s);
    if (rc != len) {
        throw ConnectionError(this->GetLastErrorMsg());
    }
}

string SftpConnection::GetLastErrorMsg() {
    char *errmsg;
    libssh2_session_last_error(this->session_, &errmsg, NULL, 0);
    string s = string(errmsg);
    return s;
}
