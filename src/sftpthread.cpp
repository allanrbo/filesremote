// Copyright 2020 Allan Riordan Boll

#include "src/sftpthread.h"

#ifdef __WXMSW__
#include <winsock2.h>  // Several header files include windows.h, but winsock2.h needs to come first.
#endif

#include <wx/secretstore.h>
#include <wx/wx.h>

#include <chrono>  // NOLINT
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/channel.h"
#include "src/direntry.h"
#include "src/hostdesc.h"
#include "src/sftpconnection.h"

using std::chrono::seconds;
using std::get_if;
using std::make_unique;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::variant;
using std::vector;

template<typename T>
static void respondToUIThread(wxEvtHandler *response_dest, int id, const T &payload) {
    wxThreadEvent event(wxEVT_THREAD, id);
    event.SetPayload(payload);
    wxQueueEvent(response_dest, event.Clone());
}

static void respondToUIThread(wxEvtHandler *response_dest, int id) {
    wxThreadEvent event(wxEVT_THREAD, id);
    wxQueueEvent(response_dest, event.Clone());
}

void sftpThreadFunc(
        wxEvtHandler *response_dest,
        shared_ptr<Channel<threadFuncVariant>> cmd_channel,
        shared_ptr<Channel<bool>> cancellation_channel) {
    unique_ptr<SftpConnection> sftp_connection;

    auto cancel = [&] {
        auto r = cancellation_channel->TryGet();
        return r.has_value() && r;
    };

    while (1) {
        auto cmd_opt = cmd_channel->Get(seconds(15));

        // Too early to have received any real cancellations, so remove any old ones there may be.
        cancellation_channel->Clear();

        threadFuncVariant cmd;
        try {
            if (cmd_opt.has_value()) {
                cmd = *cmd_opt;
            } else if (!sftp_connection->home_dir_.empty()) {
                sftp_connection->SendKeepAlive();
                continue;
            } else {
                continue;
            }

            if (get_if<SftpThreadCmdShutdown>(&cmd)) {
                return;  // Destructor of sftp_connection will be called.
            }

            if (get_if<SftpThreadCmdConnect>(&cmd)) {
                auto m = get_if<SftpThreadCmdConnect>(&cmd);

                sftp_connection = make_unique<SftpConnection>(m->host_desc);

                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_NEED_FINGERPRINT_APPROVAL,
                                  SftpThreadResponseNeedFingerprintApproval{sftp_connection->fingerprint_});
                continue;
            }

            if (get_if<SftpThreadCmdFingerprintApproved>(&cmd)) {
                if (!sftp_connection->AgentAuth()) {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_NEED_PASSWD);
                    continue;
                }

                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_CONNECTED,
                                  SftpThreadResponseConnected{sftp_connection->home_dir_});
                continue;
            }

            if (get_if<SftpThreadCmdPassword>(&cmd)) {
                auto m = get_if<SftpThreadCmdPassword>(&cmd);
                if (!sftp_connection->PasswordAuth(m->password)) {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_ERROR_AUTH);
                    continue;
                }

                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_CONNECTED,
                                  SftpThreadResponseConnected{sftp_connection->home_dir_});
                continue;
            }

            if (get_if<SftpThreadCmdGetDir>(&cmd)) {
                auto m = get_if<SftpThreadCmdGetDir>(&cmd);
                auto dir_list = sftp_connection->GetDir(m->dir);
                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_GET_DIR,
                                  SftpThreadResponseGetDir{m->dir, dir_list});
                continue;
            }

            if (get_if<SftpThreadCmdDownload>(&cmd)) {
                auto m = get_if<SftpThreadCmdDownload>(&cmd);

                // If it turns out to be a dir, it's probably because it's a symlink.
                auto dir_entry = sftp_connection->Stat(m->remote_path);
                if (dir_entry.has_value() && LIBSSH2_SFTP_S_ISDIR(dir_entry->mode_)) {
                    auto real_path = sftp_connection->RealPath(m->remote_path);
                    respondToUIThread(
                            response_dest,
                            ID_SFTP_THREAD_RESPONSE_FOLLOW_SYMLINK_DIR,
                            SftpThreadResponseFollowSymlinkDir{m->remote_path, real_path});
                    continue;
                }

                bool completed = sftp_connection->DownloadFile(m->remote_path, m->local_path, cancel);
                if (completed) {
                    respondToUIThread(
                            response_dest,
                            ID_SFTP_THREAD_RESPONSE_DOWNLOAD,
                            SftpThreadResponseDownload{m->local_path, m->remote_path, m->open_in_editor});
                } else {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_CANCELLED);
                }
                continue;
            }

            if (get_if<SftpThreadCmdUploadOverwrite>(&cmd)) {
                auto m = get_if<SftpThreadCmdUploadOverwrite>(&cmd);
                bool completed = sftp_connection->UploadFile(m->local_path, m->remote_path, cancel);
                if (completed) {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_UPLOAD,
                                      SftpThreadResponseUpload{m->remote_path});
                } else {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_CANCELLED);
                }
                continue;
            }

            if (get_if<SftpThreadCmdUpload>(&cmd)) {
                auto m = get_if<SftpThreadCmdUpload>(&cmd);

                auto dir_entry = sftp_connection->Stat(m->remote_path);
                if (dir_entry.has_value()) {
                    if (dir_entry->is_dir_) {
                        respondToUIThread(
                                response_dest,
                                ID_SFTP_THREAD_RESPONSE_DIR_ALREADY_EXISTS,
                                SftpThreadResponseDirectoryAlreadyExists{m->remote_path});
                        continue;
                    }

                    respondToUIThread(
                            response_dest,
                            ID_SFTP_THREAD_RESPONSE_CONFIRM_OVERWRITE,
                            SftpThreadResponseConfirmOverwrite{m->local_path, m->remote_path});
                    continue;
                }

                bool completed = sftp_connection->UploadFile(m->local_path, m->remote_path, cancel);
                if (completed) {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_UPLOAD,
                                      SftpThreadResponseUpload{m->remote_path});
                } else {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_CANCELLED);
                }
                continue;
            }

            if (get_if<SftpThreadCmdRename>(&cmd)) {
                auto m = get_if<SftpThreadCmdRename>(&cmd);
                sftp_connection->Rename(m->remote_old_path, m->remote_new_path);
                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUCCESS);
                continue;
            }

            if (get_if<SftpThreadCmdDelete>(&cmd)) {
                auto m = get_if<SftpThreadCmdDelete>(&cmd);
                sftp_connection->Delete(m->remote_path);
                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_DELETE_SUCCEEDED);
                continue;
            }

            if (get_if<SftpThreadCmdMkdir>(&cmd)) {
                auto m = get_if<SftpThreadCmdMkdir>(&cmd);
                sftp_connection->Mkdir(m->remote_path);
                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUCCESS);
                continue;
            }

            if (get_if<SftpThreadCmdMkfile>(&cmd)) {
                auto m = get_if<SftpThreadCmdMkfile>(&cmd);
                sftp_connection->Mkfile(m->remote_path);
                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUCCESS);
                continue;
            }

            if (get_if<SftpThreadCmdGoTo>(&cmd)) {
                auto m = get_if<SftpThreadCmdGoTo>(&cmd);

                auto dir_entry = sftp_connection->Stat(m->remote_path);
                if (!dir_entry.has_value()) {
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_FILE_NOT_FOUND,
                                      SftpThreadResponseFileError{m->remote_path, cmd});
                    continue;
                }

                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_GO_TO,
                                  SftpThreadResponseGoTo{m->remote_path, dir_entry->is_dir_});
                continue;
            }

            if (get_if<SftpThreadCmdSudo>(&cmd)) {
                auto m = get_if<SftpThreadCmdSudo>(&cmd);
                sftp_connection->sudo_passwd_ = m->password;

                if (!sftp_connection->CheckSudoInstalled()) {
                    string msg = "sudo not found on the remote machine";
                    respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUDO_FAILED,
                                      SftpThreadResponseError{msg});
                    continue;
                }

                if (sftp_connection->CheckSudoNeedsPasswd()) {
                    if (!m->password.IsOk()) {
                        respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUDO_NEEDS_PASSWD);
                        continue;
                    }

                    sftp_connection->VerifySudoPasswd();
                }

                sftp_connection->SudoEnter();
                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUDO_SUCCEEDED);
                continue;
            }

            if (get_if<SftpThreadCmdSudoExit>(&cmd)) {
                sftp_connection->SudoExit();
                sftp_connection->sudo_passwd_ = wxSecretValue();
                respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUDO_EXIT_SUCCEEDED);
                continue;
            }
        } catch (DownloadFailed e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_DOWNLOAD_FAILED,
                              SftpThreadResponseFileError{e.remote_path_, cmd});
        } catch (DownloadFailedPermission e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_DOWNLOAD_FAILED_PERMISSION,
                              SftpThreadResponseFileError{e.remote_path_, cmd});
        } catch (UploadFailed e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_UPLOAD_FAILED,
                              SftpThreadResponseFileError{e.remote_path_, cmd});
        } catch (FailedPermission e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_PERMISSION,
                              SftpThreadResponseFileError{e.remote_path_, cmd});
        } catch (UploadFailedSpace e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_UPLOAD_FAILED_SPACE,
                              SftpThreadResponseFileError{e.remote_path_, cmd});
        } catch (DirListFailedPermission e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_DIR_LIST_FAILED,
                              SftpThreadResponseFileError{e.remote_path_, cmd});
        } catch (DeleteFailed e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_DELETE_FAILED,
                              SftpThreadResponseDeleteError{e.remote_path_, e.err_, cmd});
        } catch (FileNotFound e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_FILE_NOT_FOUND,
                              SftpThreadResponseFileError{e.remote_path_, cmd});
        } catch (SudoFailed e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_SUDO_FAILED,
                              SftpThreadResponseError{e.msg_});
        } catch (ConnectionError e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_ERROR_CONNECTION,
                              SftpThreadResponseError{e.msg_});
        } catch (exception e) {
            respondToUIThread(response_dest, ID_SFTP_THREAD_RESPONSE_ERROR_CONNECTION,
                              SftpThreadResponseError{e.what()});
        }
    }
}
