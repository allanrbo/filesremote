// Copyright 2020 Allan Riordan Boll

#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <filesystem>
#include <sstream>

#ifdef __WXMSW__

#include <winsock2.h>

#else

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif

#ifdef __WXGTK__

#include "icon/icon_48x48.xpm"

#endif


#include <stdio.h>

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/cmdline.h>
#include <wx/utils.h>
#include <wx/stdpaths.h>
#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/preferences.h>
#include <wx/dataview.h>
#include <wx/aboutdlg.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include "./licensestrings.h"

using std::string;
using std::to_string;
using std::runtime_error;
using std::vector;
using std::unordered_set;
using std::copy;
using std::unique_ptr;
using std::stringstream;
using std::make_unique;
using std::filesystem::file_time_type;
using std::filesystem::create_directories;
using std::filesystem::last_write_time;
using std::filesystem::remove;

#define BUFLEN 4096
#define ID_SET_DIR 10
#define ID_REFRESH 20
#define ID_PARENT_DIR 30
#define ID_SHOW_LICENSES 40
#define ID_OPEN_SELECTED 50


void showException() {
    wxString error;
    try {
        throw;  // Rethrow the current exception in order to pattern match it here.
    } catch (const std::exception &e) {
        error = e.what();
    } catch (...) {
        error = "unknown error";
    }

    wxLogError("%s", error);
}


string normalize_path(string path) {
    // TODO(allan): support UTF-8 paths...
    replace(path.begin(), path.end(), '\\', '/');

    stringstream s(path);
    string segment;
    vector<string> parts;
    while (getline(s, segment, '/')) {
        if (segment.empty() || segment == ".") {
            continue;
        } else if (segment == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            } else {
                continue;
            }
        } else {
            parts.push_back(segment);
        }
    }

    string r = "";
    for (int i = 0; i < parts.size(); ++i) {
        if (i == 0 && parts[0].length() == 2 && parts[0][1] == ':') {
            // Windows drive letter part.
        } else {
            r += "/";
        }
        r += parts[i];
    }

    if (r.empty()) {
        return "/";
    }

    return r;
}


class DirEntry {
public:
    string name;
    uint64_t size = 0;
    uint64_t modified = 0;
    uint64_t mode;
    string mode_str;
    string owner;
    string group;
    bool isDir;

    string modifiedFormatted() {
        auto t = wxDateTime((time_t) this->modified);
        t.MakeUTC();
        return t.FormatISOCombined(' ').ToStdString();
    }
};

class SftpConnection {
    LIBSSH2_SESSION *session = NULL;
    LIBSSH2_SFTP *sftp_session = NULL;
    LIBSSH2_SFTP_HANDLE *sftp_opendir_handle = NULL;
    LIBSSH2_SFTP_HANDLE *sftp_openfile_handle = NULL;
    FILE *local_file_handle = 0;
    int sock = 0;

public:
    string home_dir = "";
    string username;
    string host;
    int port;

    SftpConnection(string username, string host, int port) {
        this->username = username;
        this->host = host;
        this->port = port;

        int rc;

#ifdef __WXMSW__
        WSADATA wsadata;
        rc = WSAStartup(MAKEWORD(2, 0), &wsadata);
        if (rc != 0) {
            throw runtime_error("WSAStartup failed (" + to_string(rc) + ")");
        }
#endif

        rc = libssh2_init(0);
        if (rc != 0) {
            throw runtime_error("libssh2_init failed (" + to_string(rc) + ")");
        }

        this->sock = socket(AF_INET, SOCK_STREAM, 0);

        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        sin.sin_addr.s_addr = inet_addr(host.c_str());
        if (sin.sin_addr.s_addr == INADDR_NONE) {
            struct hostent *remote_host = gethostbyname(host.c_str());
            if (!remote_host) {
                throw runtime_error("gethostbyname failed");
            }

            sin.sin_addr.s_addr = *reinterpret_cast<u_long *>(remote_host->h_addr_list[0]);
        }

        if (connect(this->sock, (struct sockaddr *) (&sin), sizeof(struct sockaddr_in)) != 0) {
            throw runtime_error("connect failed");
        }

        this->session = libssh2_session_init();
        if (!this->session) {
            throw runtime_error("libssh2_session_init failed");
        }

        libssh2_session_set_blocking(this->session, 1);

        rc = libssh2_session_handshake(this->session, this->sock);
        if (rc) {
            throw runtime_error("libssh2_session_handshake failed (" + to_string(rc) + ")");
        }

        // TODO(allan): verify fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);
        //// char *userauthlist = libssh2_userauth_list(this->session, username.c_str(), username.size());

        LIBSSH2_AGENT *agent = libssh2_agent_init(this->session);
        if (!agent) {
            throw runtime_error("libssh2_agent_init failed");
        }

        if (libssh2_agent_connect(agent)) {
            throw runtime_error("libssh2_agent_connect failed");
        }

        if (libssh2_agent_list_identities(agent)) {
            throw runtime_error("libssh2_agent_list_identities failed");
        }

        struct libssh2_agent_publickey *identity, *prev_identity = NULL;
        while (1) {
            rc = libssh2_agent_get_identity(agent, &identity, prev_identity);
            if (rc != 0) {
                throw runtime_error("libssh2_agent_get_identity failed");
            }

            if (libssh2_agent_userauth(agent, username.c_str(), identity) == 0) {
                break;
            }

            prev_identity = identity;
        }

        this->sftp_session = libssh2_sftp_init(this->session);
        if (!this->sftp_session) {
            throw runtime_error("libssh2_sftp_init failed");
        }

        char buf[BUFLEN];
        rc = libssh2_sftp_realpath(this->sftp_session, ".", buf, BUFLEN);
        if (rc < 0) {
            throw runtime_error("libssh2_sftp_realpath failed (" + to_string(rc) + ")");
        }
        this->home_dir = string(buf);
    }

    vector<DirEntry> getDir(string path) {
        int rc;

        this->sftp_opendir_handle = libssh2_sftp_opendir(this->sftp_session, path.c_str());
        if (!this->sftp_opendir_handle) {
            throw runtime_error("libssh2_sftp_opendir failed");
        }

        auto files = vector<DirEntry>();
        while (1) {
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            char name[BUFLEN];
            char line[BUFLEN];
            memset(name, 0, BUFLEN);
            memset(line, 0, BUFLEN);

            rc = libssh2_sftp_readdir_ex(this->sftp_opendir_handle, name, sizeof(name), line, sizeof(line), &attrs);

            if (rc == LIBSSH2_ERROR_EAGAIN) {
                continue;
            }
            if (rc == 0) {
                break;
            }
            if (rc < 0) {
                throw runtime_error("libssh2_sftp_readdir_ex failed (" + to_string(rc) + ")");
            }

            auto d = DirEntry();

            d.name = string(name);
            if (d.name == "." || d.name == "..") {
                continue;
            }

            if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
                d.size = attrs.filesize;
            }
            if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
                d.modified = attrs.mtime;
            }
            if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
                d.mode = attrs.permissions;
                d.isDir = attrs.permissions & S_IFDIR;
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
                    d.mode_str = string(segment);
                }

                if (field_num == 2) {
                    d.owner = string(segment);
                }

                if (field_num == 3) {
                    d.group = string(segment);
                }

                field_num++;
            }

            files.push_back(d);
        }

        if (this->sftp_opendir_handle) {
            libssh2_sftp_closedir(this->sftp_opendir_handle);
            this->sftp_opendir_handle = NULL;
        }

        return files;
    }

    void downloadFile(string remoteSrcPath, string localDstPath) {
        this->sftp_openfile_handle = libssh2_sftp_open(
                this->sftp_session,
                remoteSrcPath.c_str(),
                LIBSSH2_FXF_READ,
                0);
        if (!this->sftp_openfile_handle) {
            throw runtime_error("libssh2_sftp_open failed");
        }

        this->local_file_handle = fopen(localDstPath.c_str(), "wb");

        char buf[BUFLEN];
        while (1) {
            int rc = libssh2_sftp_read(this->sftp_openfile_handle, buf, BUFLEN);
            if (rc > 0) {
                fwrite(buf, 1, rc, this->local_file_handle);
                // TODO(allan): error handling for fwrite.
            } else if (rc == 0) {
                break;
            } else {
                throw runtime_error("libssh2_sftp_read failed");
            }
        }

        libssh2_sftp_close(this->sftp_openfile_handle);

        fclose(this->local_file_handle);
        this->local_file_handle = 0;
    }

    void uploadFile(string localSrcPath, string remoteDstPath) {
        this->sftp_openfile_handle = libssh2_sftp_open(
                this->sftp_session,
                remoteDstPath.c_str(),
                LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC,
                0);
        if (!this->sftp_openfile_handle) {
            throw runtime_error("libssh2_sftp_open failed");
        }

        this->local_file_handle = fopen(localSrcPath.c_str(), "rb");

        char buf[BUFLEN];
        while (1) {
            int rc = fread(buf, 1, BUFLEN, this->local_file_handle);
            if (rc > 0) {
                int nread = rc;
                char *p = buf;
                while (nread) {
                    rc = libssh2_sftp_write(this->sftp_openfile_handle, buf, rc);
                    if (rc < 0) {
                        throw runtime_error("libssh2_sftp_write failed");
                    }
                    p += rc;
                    nread -= rc;
                }
            } else {
                // TODO(allan): error handling for fread.
                break;
            }
        }

        libssh2_sftp_close(this->sftp_openfile_handle);

        fclose(this->local_file_handle);
        this->local_file_handle = 0;
    }

    ~SftpConnection() {
        if (this->local_file_handle) {
            fclose(this->local_file_handle);
        }

        if (this->sftp_openfile_handle) {
            libssh2_sftp_close(this->sftp_openfile_handle);
        }

        if (this->sftp_opendir_handle) {
            libssh2_sftp_closedir(this->sftp_opendir_handle);
        }

        if (this->sftp_session) {
            libssh2_sftp_shutdown(this->sftp_session);
        }

        if (this->session) {
            libssh2_session_disconnect(this->session, "normal shutdown");
            libssh2_session_free(this->session);
        }

        if (this->sock) {
#ifdef __WXMSW__
            closesocket(this->sock);
#else
            close(this->sock);
#endif
        }

        libssh2_exit();
    }
};


typedef std::function<void(int)> OnItemActivatedCb;
typedef std::function<void(int)> OnColumnHeaderClickCb;

// A base class, because wxDataViewListCtrl looks best on MacOS, and wxListCtrl looks best on GTK and Windows.
class DirListCtrl {
protected:
    OnItemActivatedCb onItemActivatedCb;
    OnColumnHeaderClickCb onColumnHeaderClickCb;
    wxImageList *iconsImageList;

    int iconIdx(DirEntry entry) {
        int r = 0;
        if (entry.isDir) {
            r = 1;
        }
        return r;
    }

public:
    DirListCtrl() {
        auto ap = wxArtProvider();
        auto size = wxSize(16, 16);
        this->iconsImageList = new wxImageList(size.GetWidth(), size.GetHeight(), false, 1);
        this->iconsImageList->Add(ap.GetBitmap(wxART_NORMAL_FILE, wxART_LIST, size));
        this->iconsImageList->Add(ap.GetBitmap(wxART_FOLDER, wxART_LIST, size));
    }

    virtual void Refresh(vector<DirEntry> entries) = 0;

    virtual wxControl *GetCtrl() = 0;

    virtual void SetFocus() = 0;

    virtual void ActivateCurrent() = 0;

    virtual vector<int> GetSelected() = 0;

    virtual int GetHighlighted() = 0;

    virtual void SetSelected(vector<int>) = 0;

    virtual void SetHighlighted(int) = 0;

    void BindOnItemActivated(OnItemActivatedCb cb) {
        this->onItemActivatedCb = cb;
    }

    void BindOnColumnHeaderClickCb(OnColumnHeaderClickCb cb) {
        this->onColumnHeaderClickCb = cb;
    }
};


class DvlcDirList : public DirListCtrl {
    wxDataViewListCtrl *dvlc;

public:
    explicit DvlcDirList(wxWindow *parent) : DirListCtrl() {
        this->dvlc = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxDV_MULTIPLE | wxDV_ROW_LINES);

        // TODO(allan): wxDATAVIEW_CELL_EDITABLE?
        this->dvlc->AppendIconTextColumn("Name", wxDATAVIEW_CELL_INERT, 300);
        this->dvlc->AppendTextColumn("Size", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Modified", wxDATAVIEW_CELL_INERT, 150);
        this->dvlc->AppendTextColumn("Mode", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Owner", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Group", wxDATAVIEW_CELL_INERT, 100);

        this->dvlc->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [&](wxDataViewEvent &evt) {
            if (!evt.GetItem()) {
                return;
            }
            int i = this->dvlc->GetItemData(evt.GetItem());
            this->onItemActivatedCb(i);
        });

        this->dvlc->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, [&](wxDataViewEvent &evt) {
            this->onColumnHeaderClickCb(evt.GetColumn());
        });
    }

    void Refresh(vector<DirEntry> entries) {
        this->dvlc->DeleteAllItems();

        for (int i = 0; i < entries.size(); i++) {
            wxIcon icon = this->iconsImageList->GetIcon(this->iconIdx(entries[i]));

            wxVector<wxVariant> data;
            data.push_back(wxVariant(wxDataViewIconText(entries[i].name, icon)));
            data.push_back(wxVariant(to_string(entries[i].size)));
            data.push_back(wxVariant(entries[i].modifiedFormatted()));
            data.push_back(wxVariant(entries[i].mode_str));
            data.push_back(wxVariant(entries[i].owner));
            data.push_back(wxVariant(entries[i].group));
            this->dvlc->AppendItem(data, i);
        }
    }

    wxControl *GetCtrl() {
        return this->dvlc;
    }

    void SetFocus() {
        this->dvlc->SetFocus();
    }

    void ActivateCurrent() {
        if (this->dvlc->GetCurrentItem()) {
            int i = this->dvlc->GetItemData(this->dvlc->GetCurrentItem());
            this->onItemActivatedCb(i);
        }
    }

    vector<int> GetSelected() {
        vector<int> r;
        for (int i = 0; i < this->dvlc->GetItemCount(); ++i) {
            if (this->dvlc->IsRowSelected(i)) {
                r.push_back(i);
            }
        }
        return r;
    }

    void SetSelected(vector<int> selected) {
        wxDataViewItemArray a;
        for (int i = 0; i < selected.size(); ++i) {
            a.push_back(this->dvlc->RowToItem(selected[i]));
        }
        this->dvlc->SetSelections(a);
    }

    int GetHighlighted() {
        int i = this->dvlc->ItemToRow(this->dvlc->GetCurrentItem());
        if (i < 0) {
            return 0;
        }
        return i;
    }

    void SetHighlighted(int row) {
        this->dvlc->SetCurrentItem(this->dvlc->RowToItem(row));
    }
};


class LcDirList : public DirListCtrl {
    wxListCtrl *list_ctrl;

public:
    explicit LcDirList(wxWindow *parent) : DirListCtrl() {
        this->list_ctrl = new wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);

        this->list_ctrl->AssignImageList(this->iconsImageList, wxIMAGE_LIST_SMALL);

        this->list_ctrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
        this->list_ctrl->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(2, "Modified", wxLIST_FORMAT_LEFT, 150);
        this->list_ctrl->InsertColumn(3, "Mode", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(4, "Owner", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(5, "Owner", wxLIST_FORMAT_LEFT, 100);

        this->list_ctrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, [&](wxListEvent &evt) {
            int i = this->list_ctrl->GetItemData(evt.GetItem());
            this->onItemActivatedCb(i);
        });

        this->list_ctrl->Bind(wxEVT_LIST_COL_CLICK, [&](wxListEvent &evt) {
            this->onColumnHeaderClickCb(evt.GetColumn());
        });
    }

    wxControl *GetCtrl() {
        return this->list_ctrl;
    }

    void Refresh(vector<DirEntry> entries) {
        this->list_ctrl->DeleteAllItems();

        for (int i = 0; i < entries.size(); i++) {
            this->list_ctrl->InsertItem(i, entries[i].name, this->iconIdx(entries[i]));
            this->list_ctrl->SetItemData(i, i);
            this->list_ctrl->SetItem(i, 0, entries[i].name);
            this->list_ctrl->SetItem(i, 1, to_string(entries[i].size));
            this->list_ctrl->SetItem(i, 2, entries[i].modifiedFormatted());
            this->list_ctrl->SetItem(i, 3, entries[i].mode_str);
            this->list_ctrl->SetItem(i, 4, entries[i].owner);
            this->list_ctrl->SetItem(i, 5, entries[i].group);
        }
    }

    void SetFocus() {
        this->list_ctrl->SetFocus();
    }

    void ActivateCurrent() {
        if (this->list_ctrl->GetSelectedItemCount() > 0) {
            int i = this->list_ctrl->GetItemData(
                    this->list_ctrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED));
            this->onItemActivatedCb(i);
        }
    }


    void SetSelected(vector<int> selected) {
        for (int i = 0; i < selected.size(); ++i) {
            this->list_ctrl->SetItemState(selected[i], wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        }
    }


    vector<int> GetSelected() {
        vector<int> r;
        long cur = -1;
        while (1) {
            cur = this->list_ctrl->GetNextItem(cur, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
            if (cur == -1) {
                break;
            }

            r.push_back(cur);
        }
        return r;
    }

    int GetHighlighted() {
        if (this->list_ctrl->GetItemCount() > 0) {
            auto i = this->list_ctrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
            return this->list_ctrl->GetItemData(i);
        }
        return 0;
    }

    void SetHighlighted(int row) {
        this->list_ctrl->SetItemState(row, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
    }
};


class PreferencesPageGeneralPanel : public wxPanel {
    wxConfigBase *config;
    wxTextCtrl *text_editor;

public:
    PreferencesPageGeneralPanel(wxWindow *parent, wxConfigBase *config) : wxPanel(parent) {
        this->config = config;

        wxSizer *sizer = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer *item_sizer_editor = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText *label_editor = new wxStaticText(this, wxID_ANY, "Editor path:");
        item_sizer_editor->Add(label_editor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        item_sizer_editor->Add(5, 5, 1, wxALL, 0);
        this->text_editor = new wxTextCtrl(this, 100, wxEmptyString, wxDefaultPosition, wxSize(300, -1));
        this->Bind(wxEVT_TEXT, [&](wxCommandEvent &) {
            if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
                this->TransferDataFromWindow();
            }
        });
        item_sizer_editor->Add(this->text_editor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        sizer->Add(item_sizer_editor, 0, wxGROW | wxALL, 5);

        this->SetSizerAndFit(sizer);
    }

    virtual bool TransferDataToWindow() {
        this->text_editor->SetValue(this->config->Read("/editor", ""));
        return true;
    }

    virtual bool TransferDataFromWindow() {
        this->config->Write("/editor", this->text_editor->GetValue());
        this->config->Flush();
        return true;
    }
};

class PreferencesPageGeneral : public wxStockPreferencesPage {
    wxConfigBase *config;

public:
    explicit PreferencesPageGeneral(wxConfigBase *config) : wxStockPreferencesPage(Kind_General) {
        this->config = config;
    }

    virtual wxWindow *CreateWindow(wxWindow *parent) {
        return new PreferencesPageGeneralPanel(parent, this->config);
    }
};


class OpenedFile {
public:
    string local_path;
    string remote_path;
    file_time_type modified;
};


class SftpguiFrame : public wxFrame {
    unique_ptr<SftpConnection> sftp_connection;
    wxConfigBase *config;
    DirListCtrl *dir_list_ctrl;
    wxTextCtrl *path_text_ctrl;
    wxTimer file_watcher_timer;
    string current_dir = "/";
    vector<DirEntry> current_dir_list;
    int sort_column = 0;
    bool sort_desc = false;
    vector<OpenedFile> opened_files_local;
    string stored_highlighted = "";
    unordered_set<string> stored_selected;

public:
    SftpguiFrame(unique_ptr<SftpConnection> sftp_connection, wxConfigBase *config) : wxFrame(
            NULL,
            wxID_ANY,
            wxT("Sftpgui"),
            wxPoint(-1, -1),
            wxSize(800, 600)
    ) {
        this->sftp_connection = move(sftp_connection);
        this->current_dir = this->sftp_connection->home_dir;
        this->config = config;

#ifdef __WXMSW__
        this->SetIcon(wxIcon("aaaa"));
#elif __WXGTK__
        this->SetIcon(wxIcon(icon_48x48));
#endif

        this->SetTitle("Sftpgui - " + this->sftp_connection->username + "@" + this->sftp_connection->host +
                       ":" + to_string(this->sftp_connection->port));
        this->CreateStatusBar();

        // Restore window size and pos.
        int x = this->config->Read("/window_x", -1);
        int y = this->config->Read("/window_y", -1);
        int w = this->config->Read("/window_w", 800);
        int h = this->config->Read("/window_h", 600);
        this->Move(x, y);
        this->SetClientSize(w, h);

        // Create menus.
        auto *menuBar = new wxMenuBar();
        this->SetMenuBar(menuBar);

        auto *file_menu = new wxMenu();
        menuBar->Append(file_menu, "&File");

        // Adding refresh to the menu twice with two different hotkeys, instead of using SetAcceleratorTable.
        // It's wonky, but MacOS has trouble with non-menu accelerators when the wxDataViewListCtrl has focus.
        file_menu->Append(wxID_REFRESH, "Refresh\tF5");
        file_menu->Append(wxID_REFRESH, "Refresh\tCtrl+R");
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->refreshDir(true);
        }, wxID_REFRESH);

        file_menu->Append(ID_SET_DIR, "Change directory\tCtrl+L");
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &) {
            this->path_text_ctrl->SetFocus();
            this->path_text_ctrl->SelectAll();
        }, ID_SET_DIR);

#ifdef __WXOSX__
        file_menu->Append(ID_PARENT_DIR, "Parent directory\tCtrl+Up", wxEmptyString, wxITEM_NORMAL);
#else
        file_menu->Append(ID_PARENT_DIR, "Parent directory\tAlt+Up", wxEmptyString, wxITEM_NORMAL);
#endif
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->current_dir = normalize_path(this->current_dir + "/..");
            this->path_text_ctrl->SetValue(this->current_dir);
            this->refreshDir(false);
        }, ID_PARENT_DIR);

#ifdef __WXOSX__
        file_menu->Append(ID_OPEN_SELECTED, "Open selected item\tCtrl+Down", wxEmptyString, wxITEM_NORMAL);
#endif
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->dir_list_ctrl->ActivateCurrent();
        }, ID_OPEN_SELECTED);

        file_menu->AppendSeparator();

        file_menu->Append(wxID_PREFERENCES);
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            auto prefs_editor = new wxPreferencesEditor();
            prefs_editor->AddPage(new PreferencesPageGeneral(this->config));
            prefs_editor->Show(this);
        }, wxID_PREFERENCES);

        file_menu->AppendSeparator();

        file_menu->Append(wxID_EXIT, "E&xit", "Quit this program");
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            this->Close(true);
        }, wxID_EXIT);

        auto *help_menu = new wxMenu;
        menuBar->Append(help_menu, "&Help");

        help_menu->Append(ID_SHOW_LICENSES, "Licenses");
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            wxDialog *licenses_frame = new wxDialog(this,
                                                    wxID_ANY,
                                                    "Licenses",
                                                    wxDefaultPosition,
                                                    wxSize(600, 600),
                                                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
            wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
            wxTextCtrl *licenses_text_ctrl = new wxTextCtrl(
                    licenses_frame,
                    wxID_ANY,
                    wxString::FromAscii(licenses),
                    wxDefaultPosition,
                    wxDefaultSize,
                    wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
            sizer->Add(licenses_text_ctrl, 1, wxEXPAND | wxALL);
            licenses_frame->SetSizer(sizer);
            licenses_frame->Show();
        }, ID_SHOW_LICENSES);

        help_menu->Append(wxID_ABOUT);
        this->Bind(wxEVT_MENU, [&](wxCommandEvent &event) {
            wxAboutDialogInfo info;
            info.SetName("Sftpgui");
            info.SetVersion("0.1");
            info.SetDescription("A no-nonsense SFTP file browser");
            info.SetCopyright("(C) 2020 Allan Riordan Boll");
            wxAboutBox(info, this);
        }, wxID_ABOUT);

        // Most keyboard accelerators for menu items are automatically bound via the string in its title. However, some
        // seem to only work via SetAcceleratorTable, so setting them again here.
        // MacOS seems to ignores this table when the focus is on wxDataViewListCtrl, so we rely on the accelerators in
        // the menu item titles on MacOS.
        wxAcceleratorEntry entries[]{
                wxAcceleratorEntry(wxACCEL_ALT, WXK_UP, ID_PARENT_DIR),
        };
        wxAcceleratorTable accel(sizeof(entries), entries);
        this->SetAcceleratorTable(accel);

        // Set up a timer that will watch for changes in local files.
        this->file_watcher_timer.Bind(wxEVT_TIMER, &SftpguiFrame::onFileWatcherTimer, this);
        this->file_watcher_timer.Start(1000);

        // Main layout.
        auto *panel = new wxPanel(this);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        auto *sizer_inner_top = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(sizer_inner_top, 0, wxEXPAND | wxALL, 1);

        // Create remote path text field.
        this->path_text_ctrl = new wxTextCtrl(panel, wxID_ANY, this->current_dir, wxDefaultPosition,
                                              wxDefaultSize, wxTE_PROCESS_ENTER);
#ifdef __WXOSX__
        sizer_inner_top->Add(this->path_text_ctrl, 1, wxEXPAND | wxALL, 0);
#else
        sizer_inner_top->Add(this->path_text_ctrl, 1, wxEXPAND | wxALL, 4);
#endif
        this->path_text_ctrl->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent &event) {
            this->current_dir = this->path_text_ctrl->GetValue();
            this->refreshDir(false);
        });
        this->path_text_ctrl->Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent &evt) {
            if (evt.GetModifiers() == 0 && evt.GetKeyCode() == WXK_ESCAPE && this->path_text_ctrl->HasFocus()) {
                this->path_text_ctrl->SetValue(this->current_dir);
                this->path_text_ctrl->SelectNone();
                this->dir_list_ctrl->SetFocus();
                return;
            }

            evt.Skip();
        });

#ifdef __WXOSX__
        // On MacOS wxDataViewListCtrl looks best.
        this->dir_list_ctrl = new DvlcDirList(panel);
#else
        // On GTK and Windows wxListCtrl looks best.
        this->dir_list_ctrl = new LcDirList(panel);
//        this->dir_list_ctrl = new DvlcDirList(panel);
#endif
        sizer->Add(this->dir_list_ctrl->GetCtrl(), 1, wxEXPAND | wxALL, 0);
        this->dir_list_ctrl->SetFocus();

        this->dir_list_ctrl->BindOnItemActivated([&](int n) {
            string status = "";
            try {
                auto entry = this->current_dir_list[n];
                if (entry.isDir) {
                    this->current_dir = normalize_path(this->current_dir + "/" + entry.name);
                    this->path_text_ctrl->SetValue(this->current_dir);
                    this->refreshDir(false);
                } else {
                    this->downloadFile(entry.name);
                }
            } catch (...) {
                showException();
            }
        });

        this->dir_list_ctrl->BindOnColumnHeaderClickCb([&](int col) {
            if (this->sort_column == col) {
                this->sort_desc = !this->sort_desc;
            } else {
                this->sort_desc = false;
                this->sort_column = col;
            }

            this->rememberSelected();
            this->sortAndPopulateDir();
            this->recallSelected();
            this->dir_list_ctrl->SetFocus();
        });

        panel->SetSizerAndFit(sizer);
        this->refreshDir(false);
    }

    ~SftpguiFrame() {
        for (int i = 0; i < this->opened_files_local.size(); ++i) {
            remove(this->opened_files_local[i].local_path);
        }

        // Save frame position.
        int x, y, w, h;
        GetClientSize(&w, &h);
        GetPosition(&x, &y);
        this->config->Write("/window_x", x);
        this->config->Write("/window_y", y);
        this->config->Write("/window_w", w);
        this->config->Write("/window_h", h);
        this->config->Flush();
    }

private:
    void setIdleStatusText(string additional = "") {
        string s = "Ready";
        if (!additional.empty()) {
            s += ". " + additional;
        }
        this->SetStatusText(s);
    }

    void onFileWatcherTimer(const wxTimerEvent &event) {
        string last_upload = "";
        for (int i = 0; i < this->opened_files_local.size(); ++i) {
            OpenedFile f = this->opened_files_local[i];
            if (last_write_time(f.local_path) > f.modified) {
                this->sftp_connection->uploadFile(f.local_path, f.remote_path);
                this->opened_files_local[i].modified = last_write_time(f.local_path);
                last_upload = f.remote_path;
            }
        }

        if (!last_upload.empty()) {
            this->refreshDir(true);
            string d = string(wxDateTime::Now().FormatISOCombined());
            this->setIdleStatusText("Uploaded " + last_upload + " at " + d + ".");
        }
    }

    void rememberSelected() {
        this->stored_highlighted = this->current_dir_list[this->dir_list_ctrl->GetHighlighted()].name;
        this->stored_selected.clear();
        auto r = this->dir_list_ctrl->GetSelected();
        for (int i = 0; i < r.size(); ++i) {
            this->stored_selected.insert(this->current_dir_list[r[i]].name);
        }
    }

    void recallSelected() {
        int highlighted = 0;
        vector<int> selected;
        for (int i = 0; i < this->current_dir_list.size(); ++i) {
            if (this->stored_selected.find(this->current_dir_list[i].name) != this->stored_selected.end()) {
                selected.push_back(i);
            }
            if (this->current_dir_list[i].name == this->stored_highlighted) {
                highlighted = i;
            }
        }
        this->dir_list_ctrl->SetHighlighted(highlighted);
        this->dir_list_ctrl->SetSelected(selected);
    }

    void refreshDir(bool preserve_selection) {
        if (preserve_selection) {
            this->rememberSelected();
        }
        this->current_dir_list = this->sftp_connection->getDir(this->current_dir);
        this->sortAndPopulateDir();
        this->setIdleStatusText(string("Retrieved dir list at " + wxDateTime::Now().FormatISOCombined() + "."));
        if (preserve_selection) {
            this->recallSelected();
        }
    }

    void sortAndPopulateDir() {
        auto cmp = [&](const DirEntry &a, const DirEntry &b) {
            if (a.name == "..") { return true; }
            if (b.name == "..") { return false; }
            if (a.isDir && !b.isDir) { return true; }
            if (!a.isDir && b.isDir) { return false; }

            string a_val, b_val;
            if (this->sort_column == 1) {
                if (this->sort_desc) {
                    return a.size < b.size;
                }
                return a.size > b.size;
            } else if (this->sort_column == 2) {
                if (this->sort_desc) {
                    return a.modified < b.modified;
                }
                return a.modified > b.modified;
            } else if (this->sort_column == 3) {
                if (this->sort_desc) {
                    return a.mode_str < b.mode_str;
                }
                return a.mode_str > b.mode_str;
            } else if (this->sort_column == 4) {
                if (this->sort_desc) {
                    return a.owner < b.owner;
                }
                return a.owner > b.owner;
            } else if (this->sort_column == 5) {
                if (this->sort_desc) {
                    return a.group < b.group;
                }
                return a.group > b.group;
            }

            // Assume sort_column == 0.
            if (a.name.length() > 0 && b.name.length() > 0 && a.name[0] == '.' && b.name[0] != '.') { return true; }
            if (a.name.length() > 0 && b.name.length() > 0 && a.name[0] != '.' && b.name[0] == '.') { return false; }
            if (this->sort_desc) {
                return a.name < b.name;
            }
            return a.name > b.name;
        };
        sort(this->current_dir_list.begin(), this->current_dir_list.end(), cmp);

        this->dir_list_ctrl->Refresh(this->current_dir_list);
    }

    void downloadFile(string name) {
        string editor = string(this->config->Read("/editor", ""));
        if (editor.empty()) {
            string msg = "No text editor configured. Set one in Preferences.";
            wxMessageBox(msg, "Notice", wxOK | wxICON_INFORMATION, this);
            return;
        }

        string remote_path = normalize_path(this->current_dir + "/" + name);
        string local_tmp = string(wxStandardPaths::Get().GetTempDir());
        string local_dir = normalize_path(local_tmp + "/sftpgui/" + this->current_dir);
        string local_path = normalize_path(local_dir + "/" + name);

        // TODO(allan): restrict permissions
        create_directories(local_dir);

        this->sftp_connection->downloadFile(remote_path, local_path);

        bool previously_downloaded = false;
        for (int i = 0; i < this->opened_files_local.size(); ++i) {
            if (this->opened_files_local[i].remote_path == remote_path) {
                this->opened_files_local[i].modified = last_write_time(local_path);
                previously_downloaded = true;
                break;
            }
        }
        if (!previously_downloaded) {
            OpenedFile f;
            f.local_path = local_path;
            f.remote_path = remote_path;
            f.modified = last_write_time(local_path);
            this->opened_files_local.push_back(f);
        }

        string d = string(wxDateTime::Now().FormatISOCombined());
        this->setIdleStatusText("Downloaded " + remote_path + " at " + d + ".");

        wxExecute(editor + " " + local_path, wxEXEC_ASYNC);
    }
};


class SftpguiApp : public wxApp {
    string host;
    string username;
    int port = 22;

public:
    bool OnInit() {
        try {
            if (!wxApp::OnInit())
                return false;

            // TODO(allan): Better control the config path. ".config/sftpgui"
            auto es = wxEmptyString;
            wxFileConfig *config = new wxFileConfig("sftpgui", es, es, es, wxCONFIG_USE_LOCAL_FILE);
            config->EnableAutoSave();
            config->SetRecordDefaults();
            wxConfigBase::Set(config);

            if (host.empty()) {
                // TODO(allan): a better host selection window. Get inspired by Finder's "Connect to Server" window.
                wxTextEntryDialog dialog(0,
                                         "Enter remote host.\n"
                                         "Format: [username@]host:port\n"
                                         "Defaults to current local username and port 22 if not specified.",
                                         "Sftpgui");
                if (dialog.ShowModal() == wxID_CANCEL) {
                    return false;
                }
                if (!this->parseHost(string(dialog.GetValue()))) {
                    return false;
                }
            }

            // TODO(allan): put in thread. Maybe use wxThread. Use wxQueueEvent to wake up GUI thread from SSH thread.
            auto sftpConnection = make_unique<SftpConnection>(username, host, port);

            wxFrame *frame = new SftpguiFrame(move(sftpConnection), config);
            frame->Show();
        } catch (...) {
            showException();
            return false;
        }

        return true;
    }

    virtual void OnInitCmdLine(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        parser.SetSwitchChars(wxT("-"));
        parser.AddParam("[user@]host:port", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddSwitch("h", "help", "displays help", wxCMD_LINE_OPTION_HELP);
    }

    bool parseHost(string host) {
        this->username = wxGetUserId();
        this->host = host;
        this->port = 22;

#ifdef __WXMSW__
        transform(this->username.begin(), this->username.end(), this->username.begin(), ::tolower);
#endif

        if (this->host.find("@") != string::npos) {
            int i = this->host.find("@");
            this->username = this->host.substr(0, i);
            this->host = this->host.substr(i + 1);
        }

        if (this->host.find(":") != string::npos) {
            int i = this->host.find(":");

            string ps = string(this->host.substr(i + 1));
            if (!std::all_of(ps.begin(), ps.end(), ::isdigit)) {
                wxLogFatalError("non-digit port number");
                return false;
            }
            this->port = stoi(string(ps));
            if (!(0 < this->port && this->port < 65536)) {
                wxLogFatalError("invalid port number");
                return false;
            }

            this->host = this->host.substr(0, i);
        }

        return true;
    }

    virtual bool OnCmdLineParsed(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        if (parser.GetParamCount() > 0) {
            if (!this->parseHost(string(parser.GetParam(0)))) {
                return false;
            }
        }

        return true;
    }

    virtual bool OnExceptionInMainLoop() {
        showException();
        return false;
    }
};

IMPLEMENT_APP(SftpguiApp)
