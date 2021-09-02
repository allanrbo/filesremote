// Copyright 2020 Allan Riordan Boll

#ifndef SRC_SFTPCONNECTION_H_
#define SRC_SFTPCONNECTION_H_

#include <wx/secretstore.h>

#include <future>  // NOLINT
#include <optional>
#include <string>
#include <vector>

#include "src/direntry.h"
#include "src/hostdesc.h"
#include "src/string.h"

using std::exception;
using std::function;
using std::optional;
using std::string;
using std::vector;

class DownloadFailed : public exception {
public:
    string remote_path_;

    explicit DownloadFailed(string remote_path) : remote_path_(remote_path) {}
};

class DownloadFailedPermission : public exception {
public:
    string remote_path_;

    explicit DownloadFailedPermission(string remote_path) : remote_path_(remote_path) {}
};

class UploadFailed : public exception {
public:
    string remote_path_;

    explicit UploadFailed(string remote_path) : remote_path_(remote_path) {}
};

class FailedPermission : public exception {
public:
    string remote_path_;

    explicit FailedPermission(string remote_path) : remote_path_(remote_path) {}
};

class UploadFailedSpace : public exception {
public:
    string remote_path_;

    explicit UploadFailedSpace(string remote_path) : remote_path_(remote_path) {}
};

class DirListFailedPermission : public exception {
public:
    string remote_path_;

    explicit DirListFailedPermission(string remote_path) : remote_path_(remote_path) {}
};

class DeleteFailed : public exception {
public:
    string remote_path_;
    string err_;

    explicit DeleteFailed(string remote_path, string err) : remote_path_(remote_path), err_(err) {}
};

class FileNotFound : public exception {
public:
    string remote_path_;

    explicit FileNotFound(string remote_path) : remote_path_(remote_path) {}
};

class ConnectionError : public exception {
public:
    string msg_;

    explicit ConnectionError(string msg) : msg_(msg) {}
};

class SudoFailed : public exception {
public:
    string msg_;

    explicit SudoFailed(string msg) : msg_(msg) {}
};


class SftpConnection {
private:
    LIBSSH2_SESSION *session_ = NULL;
    LIBSSH2_SFTP *sftp_session_ = NULL;
    int sock_ = 0;
    bool sudo_ = false;
    char *userauth_list = NULL;
    LIBSSH2_CHANNEL *sudo_channel_ = NULL;
    LIBSSH2_CHANNEL *non_sudo_channel_ = NULL;

public:
    string home_dir_ = "";
    HostDesc host_desc_;
    string fingerprint_ = "";
    wxSecretValue sudo_passwd_ = wxSecretValue();

    explicit SftpConnection(HostDesc host_desc);

    vector<DirEntry> GetDir(string path);

    bool DownloadFile(
            string remote_src_path,
            string local_dst_path,
            function<bool(void)> cancelled,
            function<void(string, uint64_t, uint64_t, uint64_t)> progress);

    bool UploadFile(
            string local_src_path,
            string remote_dst_path,
            function<bool(void)> cancelled,
            function<void(string, uint64_t, uint64_t, uint64_t)> progress);

    optional<DirEntry> Stat(string remote_path);

    ~SftpConnection();

    void Rename(string remote_old_path, string remote_new_path);

    void Delete(string remote_path);

    void Mkdir(string remote_path);

    void Mkfile(string remote_path);

    string RealPath(string remote_path);

    bool PasswordAuth(wxSecretValue passwd);

    bool AgentAuth();

    bool KeyAuth(string identity_file);

    bool KeyAuthAutoDetect();

    void SendKeepAlive();

    bool CheckSudoInstalled();

    bool CheckSudoNeedsPasswd();

    void VerifySudoPasswd();

    void SftpSubsystemInit();

    void SudoEnter(bool needs_passwd_again);

    void SudoExit();

private:
    string GetLastErrorMsg();

    void SendSudoPasswd(LIBSSH2_CHANNEL *channel);

    void VerifySudoStillValid();
};

#endif  // SRC_SFTPCONNECTION_H_
