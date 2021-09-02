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
#include "src/ids.h"

using std::shared_ptr;
using std::string;
using std::variant;
using std::vector;

struct SftpThreadCmdConnect {
    HostDesc host_desc;
};

struct SftpThreadResponseNeedFingerprintApproval {
    string fingerprint;
};

struct SftpThreadCmdFingerprintApproved {
    string identity_file;
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

struct SftpThreadResponseProgress {
    string remote_path;
    uint64_t bytes_done;
    uint64_t bytes_total;
    uint64_t bytes_per_sec;
};

struct SftpThreadCmdSudo {
    wxSecretValue password;
};

struct SftpThreadCmdSudoExit {
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
        SftpThreadCmdGoTo,
        SftpThreadCmdSudo,
        SftpThreadCmdSudoExit
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
