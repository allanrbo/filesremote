// Copyright 2020 Allan Riordan Boll

#ifndef SRC_SFTPTHREAD_H_
#define SRC_SFTPTHREAD_H_

#ifdef __WXMSW__
#include <winsock2.h>  // Several header files include windows.h, but winsock2.h needs to come first.
#endif

#include <wx/secretstore.h>
#include <wx/wx.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/channel.h"
#include "src/direntry.h"
#include "src/hostdesc.h"

using std::shared_ptr;
using std::string;
using std::variant;
using std::vector;

#define ID_SFTP_THREAD_RESPONSE_CONNECTED 10
#define ID_SFTP_THREAD_RESPONSE_GET_DIR 20
#define ID_SFTP_THREAD_RESPONSE_NEED_PASSWD 30
#define ID_SFTP_THREAD_RESPONSE_NEED_FINGERPRINT_APPROVAL 40
#define ID_SFTP_THREAD_RESPONSE_ERROR_AUTH 50
#define ID_SFTP_THREAD_RESPONSE_UPLOAD 60
#define ID_SFTP_THREAD_RESPONSE_ERROR_CONNECTION 70
#define ID_SFTP_THREAD_RESPONSE_DOWNLOAD 80
#define ID_SFTP_THREAD_RESPONSE_DOWNLOAD_FAILED 90
#define ID_SFTP_THREAD_RESPONSE_DOWNLOAD_FAILED_PERMISSION 100
#define ID_SFTP_THREAD_RESPONSE_UPLOAD_FAILED 110
#define ID_SFTP_THREAD_RESPONSE_PERMISSION 120
#define ID_SFTP_THREAD_RESPONSE_UPLOAD_FAILED_SPACE 130
#define ID_SFTP_THREAD_RESPONSE_DIR_ALREADY_EXISTS 140
#define ID_SFTP_THREAD_RESPONSE_DIR_LIST_FAILED 150
#define ID_SFTP_THREAD_RESPONSE_FILE_NOT_FOUND 160
#define ID_SFTP_THREAD_RESPONSE_CONFIRM_OVERWRITE 170
#define ID_SFTP_THREAD_RESPONSE_CANCELLED 180
#define ID_SFTP_THREAD_RESPONSE_SUCCESS 190
#define ID_SFTP_THREAD_RESPONSE_DELETE_FAILED 200
#define ID_SFTP_THREAD_RESPONSE_DELETE_SUCCEEDED 210
#define ID_SFTP_THREAD_RESPONSE_FOLLOW_SYMLINK_DIR 220
#define ID_SFTP_THREAD_RESPONSE_GO_TO 230

struct SftpThreadCmdConnect {
    HostDesc host_desc;
};

struct SftpThreadResponseNeedFingerprintApproval {
    string fingerprint;
};

struct SftpThreadCmdFingerprintApproved {
};

struct SftpThreadCmdPassword {
    wxSecretValue password;
};

struct SftpThreadResponseConnected {
    string home_dir;
};

struct SftpThreadCmdShutdown {
};

struct SftpThreadCmdGetDir {
    string dir;
};

struct SftpThreadResponseGetDir {
    string dir;
    vector<DirEntry> dir_list;
};

struct SftpThreadResponseError {
    string error;
};

struct SftpThreadCmdUpload {
    string local_path;
    string remote_path;
};

struct SftpThreadCmdUploadOverwrite {
    string local_path;
    string remote_path;
};

struct SftpThreadResponseUpload {
    string remote_path;
};

struct SftpThreadResponseConfirmOverwrite {
    string local_path;
    string remote_path;
};

struct SftpThreadCmdDownload {
    string local_path;
    string remote_path;
    bool open_in_editor;
};

struct SftpThreadResponseDownload {
    string local_path;
    string remote_path;
    bool open_in_editor;
};

struct SftpThreadResponseDirectoryAlreadyExists {
    string remote_path;
};

struct SftpThreadCmdRename {
    string remote_old_path;
    string remote_new_path;
};

struct SftpThreadCmdDelete {
    string remote_path;
};

struct SftpThreadCmdMkdir {
    string remote_path;
};

struct SftpThreadCmdMkfile {
    string remote_path;
};

struct SftpThreadCmdGoTo {
    string remote_path;
};

struct SftpThreadResponseGoTo {
    string remote_path;
    bool is_dir;
};

struct SftpThreadResponseFollowSymlinkDir {
    string symlink_path;
    string real_path;
};

// It would be much more elegant to use std::any, but it is unavailable in MacOS 10.13.
typedef variant<
        SftpThreadCmdShutdown,
        SftpThreadCmdConnect,
        SftpThreadCmdFingerprintApproved,
        SftpThreadCmdPassword,
        SftpThreadCmdGetDir,
        SftpThreadCmdDownload,
        SftpThreadCmdUpload,
        SftpThreadCmdUploadOverwrite,
        SftpThreadCmdRename,
        SftpThreadCmdDelete,
        SftpThreadCmdMkdir,
        SftpThreadCmdMkfile,
        SftpThreadCmdGoTo
> threadFuncVariant;

struct SftpThreadResponseFileError {
    string remote_path;
    threadFuncVariant cmd;
};

struct SftpThreadResponseDeleteError {
    string remote_path;
    string err;
    threadFuncVariant cmd;
};

void sftpThreadFunc(
        wxEvtHandler *response_dest,
        shared_ptr<Channel<threadFuncVariant>> cmd_channel,
        shared_ptr<Channel<bool>> cancellation_channel);

#endif  // SRC_SFTPTHREAD_H_
