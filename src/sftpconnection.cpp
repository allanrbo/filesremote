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
                throw DownloadFailedPermission(remote_path);
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

    ChannelHandle channel(libssh2_channel_open_session(this->session_));
    if (!channel.channel_) {
        throw ConnectionError("libssh2_channel_open_session failed. " + this->GetLastErrorMsg());
    }

    remote_path = regex_replace(remote_path, regex("\""), "\\\"");
    rc = libssh2_channel_exec(channel.channel_, ("rm -fr \"" + remote_path + "\"").c_str());
    if (rc != 0) {
        throw ConnectionError("libssh2_channel_exec failed. " + this->GetLastErrorMsg());
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
    auto sftp_openfile_handle_ = SftpHandle(libssh2_sftp_open(
            this->sftp_session_,
            remote_path.c_str(),
            LIBSSH2_FXF_CREAT,
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

bool SftpConnection::PasswordAuth(wxSecretValue passwd) {
    // TODO(allan): check first if password auth is even supported...
    //// char *userauthlist = libssh2_userauth_list(this->session, username.c_str(), username.size());

    auto p = reinterpret_cast<const char *>(passwd.GetData());
    char *s = reinterpret_cast<char *>(malloc(passwd.GetSize() + 1));
    memcpy(s, p, passwd.GetSize());
    s[passwd.GetSize()] = 0;  // Null terminate.
    int rc = libssh2_userauth_password(this->session_, this->host_desc_.username_.c_str(), s);
    wxSecretValue::Wipe(passwd.GetSize() + 1, s);
    free(s);
    if (rc) {
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
    this->Stat(".");
}

void SftpConnection::SftpSubsystemInit() {
    this->sftp_session_ = libssh2_sftp_init(this->session_);
    if (!this->sftp_session_) {
        throw ConnectionError("libssh2_sftp_init failed. " + this->GetLastErrorMsg());
    }

    this->home_dir_ = this->RealPath(".");
}

string SftpConnection::GetLastErrorMsg() {
    char *errmsg;
    libssh2_session_last_error(this->session_, &errmsg, NULL, 0);
    string s = string(errmsg);
    return s;
}
