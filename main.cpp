// Copyright 2020 Allan Riordan Boll

#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
#include <filesystem>
#include <sstream>

#ifdef _WIN32

#include <winsock2.h>

#else

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

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

#include "licensestrings.h"

using std::string;
using std::to_string;
using std::runtime_error;
using std::vector;
using std::unique_ptr;
using std::stringstream;
using std::make_unique;
using std::filesystem::file_time_type;
using std::filesystem::create_directories;
using std::filesystem::last_write_time;
using std::filesystem::remove;

#define BUFLEN 4096
#define EVT_ACCEL_CTRL_L 10
#define EVT_ACCEL_F5 20
#define EVT_ACCEL_ALT_UP 30
#define EVT_LICENSES_MENU 40

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

#ifdef WIN32
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
            if (d.name == ".") {
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

                // TODO(allan): is line always guaranteed to have the mode string?
                char mode_c_str[11];
                memcpy(mode_c_str, line, 10);
                mode_c_str[10] = '\0';
                d.mode_str = string(mode_c_str);
            }

            // Extract user and group from the free text line.
            stringstream s(line);
            string segment;
            vector<string> parts;
            int field_num = 0;
            while (getline(s, segment, ' ')) {
                if (segment.empty()) {
                    continue;
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
#ifdef WIN32
            closesocket(this->sock);
#else
            close(this->sock);
#endif
        }

        libssh2_exit();
    }
};


typedef std::function<void(int)> OnItemActivatedCb;

// A base class, because wxDataViewListCtrl looks best on MacOS, and wxListCtrl looks best on GTK and Windows.
class DirListCtrl {
protected:
    OnItemActivatedCb onItemActivatedCb;
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

    void BindOnItemActivated(OnItemActivatedCb cb) {
        this->onItemActivatedCb = cb;
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
        this->dvlc->AppendTextColumn("Permissions", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Owner", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Group", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->wxDataViewListCtrl::Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &DvlcDirList::onItemActivated, this);
        this->dvlc->SetFocus();
    }

    void onItemActivated(const wxDataViewEvent &event) {
        int i = this->dvlc->GetItemData(event.GetItem());
        this->onItemActivatedCb(i);
    }

    void Refresh(vector<DirEntry> entries) {
        // TODO(allan): make into a field. Or actually make the individual icons into fields
        auto ap = wxArtProvider();
        auto size = wxSize(16, 16);

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

        // TODO(allan): remove this test
//        this->dvlc->SelectRow(4);
//        this->dvlc->SetCurrentItem(this->dvlc->RowToItem(6));
    }

    wxControl *GetCtrl() {
        return this->dvlc;
    }
};


int wxCALLBACK
MyCompareFunction(wxIntPtr item1, wxIntPtr item2, wxIntPtr WXUNUSED(sortData)) {
    if (item1 < item2)
        return 1;
    if (item1 > item2)
        return -1;

    return 0;
}


class LcDirList : public DirListCtrl {
    wxListCtrl *list_ctrl;

public:
    explicit LcDirList(wxWindow *parent) : DirListCtrl() {
        this->list_ctrl = new wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);

        this->list_ctrl->AssignImageList(this->iconsImageList, wxIMAGE_LIST_SMALL);

        this->list_ctrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
        this->list_ctrl->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(2, "Modified", wxLIST_FORMAT_LEFT, 150);
        this->list_ctrl->InsertColumn(3, "Permissions", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(4, "Owner", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(5, "Owner", wxLIST_FORMAT_LEFT, 100);

        this->list_ctrl->wxListCtrl::Bind(wxEVT_LIST_ITEM_ACTIVATED, &LcDirList::onItemActivated, this);

        this->list_ctrl->SetFocus();
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

        // TODO(allan): remove this test
//        this->list_ctrl->SetItemState(4, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);

        //this->list_ctrl->SortItems(MyCompareFunction, 0);
    }

    void onItemActivated(const wxListEvent &event) {
        int i = this->list_ctrl->GetItemData(event.GetItem());
        this->onItemActivatedCb(i);
    }
};

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
        this->Bind(wxEVT_TEXT, &PreferencesPageGeneralPanel::onPreferenceChanged, this);
        item_sizer_editor->Add(this->text_editor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

        sizer->Add(item_sizer_editor, 0, wxGROW | wxALL, 5);

        this->SetSizerAndFit(sizer);
    }

    void onPreferenceChanged(const wxCommandEvent &event) {
        if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
            this->TransferDataFromWindow();
        }
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
    vector<OpenedFile> opened_files_local;

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
        auto *fileMenu = new wxMenu;
        menuBar->Append(fileMenu, "&File");
        fileMenu->Append(wxID_PREFERENCES);
        this->Bind(wxEVT_MENU, &SftpguiFrame::onPreferencesMenuItem, this, wxID_PREFERENCES);
        fileMenu->Append(wxID_EXIT, "E&xit", "Quit this program");
        this->Bind(wxEVT_MENU, &SftpguiFrame::onQuitMenuItem, this, wxID_EXIT);
        auto *helpMenu = new wxMenu;
        menuBar->Append(helpMenu, "&Help");
        helpMenu->Append(EVT_LICENSES_MENU, "Licenses");
        this->Bind(wxEVT_MENU, &SftpguiFrame::onLicensesMenuItem, this, EVT_LICENSES_MENU);
        helpMenu->Append(wxID_ABOUT);
        this->Bind(wxEVT_MENU, &SftpguiFrame::onAboutMenuItem, this, wxID_ABOUT);
        SetMenuBar(menuBar);

        // Set up shortcut keys.
        wxAcceleratorEntry entries[]{
                wxAcceleratorEntry(wxACCEL_CTRL, (int) 'L', EVT_ACCEL_CTRL_L),
                wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F5, EVT_ACCEL_F5),
                wxAcceleratorEntry(wxACCEL_ALT, WXK_UP, EVT_ACCEL_ALT_UP),
        };
        wxAcceleratorTable accel(sizeof(entries), entries);
        this->SetAcceleratorTable(accel);
        this->Bind(wxEVT_MENU, &SftpguiFrame::onCtrlL, this, EVT_ACCEL_CTRL_L);
        this->Bind(wxEVT_MENU, &SftpguiFrame::onF5, this, EVT_ACCEL_F5);
        this->Bind(wxEVT_MENU, &SftpguiFrame::onAltUp, this, EVT_ACCEL_ALT_UP);

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
        sizer_inner_top->Add(this->path_text_ctrl, 1, wxEXPAND | wxALL, 4);
        this->Bind(wxEVT_TEXT_ENTER, &SftpguiFrame::onPathTextEnter, this);

#ifdef __APPLE__
        // On MacOS wxDataViewListCtrl looks best.
        this->dir_list_ctrl = new DvlcDirList(panel);
#else
        // On GTK and Windows wxListCtrl looks best.
        this->dir_list_ctrl = new LcDirList(panel);
//        this->dir_list_ctrl = new DvlcDirList(mainPane);
#endif
        sizer->Add(this->dir_list_ctrl->GetCtrl(), 1, wxEXPAND | wxALL, 0);

        // TODO(allan): exception handling in this lambda...
        this->dir_list_ctrl->BindOnItemActivated([&](int n) {
            string status = "";
            try {
                auto entry = this->current_dir_list[n];
                if (entry.isDir) {
                    this->current_dir = normalize_path(this->current_dir + "/" + entry.name);
                    this->path_text_ctrl->SetValue(this->current_dir);
                    this->refreshDir();
                } else {
                    this->downloadFile(entry.name);
                }
            } catch (...) {
                showException();
            }
        });

        panel->SetSizerAndFit(sizer);
        this->refreshDir();
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
            this->refreshDir();
            string d = string(wxDateTime::Now().FormatISOCombined());
            this->setIdleStatusText("Uploaded " + last_upload + " at " + d + ".");
        }
    }

    void onPathTextEnter(const wxCommandEvent &event) {
        this->current_dir = this->path_text_ctrl->GetValue();
        this->refreshDir();
    }

    void onCtrlL(const wxCommandEvent &event) {
        this->path_text_ctrl->SetFocus();
        this->path_text_ctrl->SelectAll();
    }

    void onF5(const wxCommandEvent &event) {
        this->refreshDir();
    }

    void onAltUp(const wxCommandEvent &event) {
        this->current_dir = normalize_path(this->current_dir + "/..");
        this->path_text_ctrl->SetValue(this->current_dir);
        this->refreshDir();
    }

    void onLicensesMenuItem(const wxCommandEvent &event) {
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
    }

    void onAboutMenuItem(const wxCommandEvent &event) {
        wxAboutDialogInfo info;
        info.SetName("Sftpgui");
        info.SetVersion("0.1");
        info.SetDescription("A no-nonsense SFTP file browser");
        info.SetCopyright("(C) 2020 Allan Riordan Boll");
        wxAboutBox(info, this);
    }

    void refreshDir() {
        this->current_dir_list = this->sftp_connection->getDir(this->current_dir);

        auto cmp = [](const DirEntry &a, const DirEntry &b) {
            if (a.name == "..") {
                return true;
            }
            if (b.name == "..") {
                return false;
            }
            if (a.isDir && !b.isDir) {
                return true;
            }
            if (!a.isDir && b.isDir) {
                return false;
            }
            if (a.name.length() > 0 && b.name.length() > 0 && a.name[0] == '.' && b.name[0] != '.') {
                return true;
            }
            if (a.name.length() > 0 && b.name.length() > 0 && a.name[0] != '.' && b.name[0] == '.') {
                return false;
            }

            return a.name > b.name;
        };
        sort(this->current_dir_list.begin(), this->current_dir_list.end(), cmp);


        this->dir_list_ctrl->Refresh(this->current_dir_list);
        this->setIdleStatusText(string("Fetched dir at " + wxDateTime::Now().FormatISOCombined() + "."));
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

    void onPreferencesMenuItem(const wxCommandEvent &event) {
        auto prefs_editor = new wxPreferencesEditor();
        prefs_editor->AddPage(new PreferencesPageGeneral(this->config));
        prefs_editor->Show(this);
    }

    void onQuitMenuItem(const wxCommandEvent &event) {
        Close(true);
    }
};


class HostSelectionDialog : public wxDialog {
    wxConfigBase *config;
    wxTextCtrl *host;
    wxTextCtrl *user;


public:
    HostSelectionDialog(wxConfigBase *config) : wxDialog(NULL, -1, "Sftpgui") {
        this->config = config;

        this->Bind(wxEVT_CLOSE_WINDOW, &HostSelectionDialog::OnClose, this);

        // Main layout.
        auto *panel = new wxPanel(this);
        auto *sizer = new wxBoxSizer(wxVERTICAL);

//        auto *sizer_buttons = this->CreateButtonSizer(wxOK);
//        sizer->Add(sizer_buttons, 0, wxALIGN_RIGHT);

        auto *sizer_buttons = new wxBoxSizer(wxTB_HORIZONTAL);
        sizer->Add(sizer_buttons, 0, wxALIGN_RIGHT);
        auto *button_connect = new wxButton(this, wxID_OK, "Connect"); // , wxDefaultPosition, wxSize(70, 30)
        sizer_buttons->Add(button_connect, 0, wxRIGHT, 10);
        button_connect->SetDefault();



        this->SetSizer(sizer);


//        auto *sizer_buttons = this->CreateButtonSizer(0);


//        auto *sizer_host = new wxBoxSizer(wxHORIZONTAL);
//        auto *label_host = new wxStaticText(this, wxID_ANY, "Host:");
//        sizer_host->Add(label_host, 0, wxALL | wxALIGN_CENTER_VERTICAL);
//        sizer_host->Add(0, 0, 1, wxALL);
//        this->host = new wxTextCtrl(this,
//                                    wxID_ANY); // , wxEmptyString, wxDefaultPosition, wxSize(300, -1), wxTE_PROCESS_ENTER
//        sizer_host->Add(this->host, 0, wxALL | wxALIGN_CENTER_VERTICAL);
//        vbox->Add(sizer_host, 0, wxGROW | wxALL, 2);
//
//        auto *sizer_user = new wxBoxSizer(wxHORIZONTAL);
//        auto *label_user = new wxStaticText(this, wxID_ANY, "Username:");
//        sizer_user->Add(label_user, 0, wxALL | wxALIGN_CENTER_VERTICAL);
//        sizer_user->Add(0, 0, 1, wxALL);
//        this->user = new wxTextCtrl(this,
//                                    wxID_ANY); // , wxEmptyString, wxDefaultPosition, wxSize(300, -1), wxTE_PROCESS_ENTER
//        sizer_user->Add(this->user, 0, wxALL | wxALIGN_CENTER_VERTICAL);
//        vbox->Add(sizer_user, 0, wxGROW | wxALL, 2);
//
//        auto *sizer_buttons = this->CreateButtonSizer(0);
//        wxButton *okButton = new wxButton(this, wxID_OK, "Connect"); // , wxDefaultPosition, wxSize(70, 30)
//        okButton->SetDefault();
//        sizer_buttons->Add(okButton, 1);
//        wxButton *closeButton = new wxButton(this, wxID_CANCEL, "Close", wxDefaultPosition, wxSize(70, 30));
//        this->Bind(wxEVT_BUTTON, &HostSelectionDialog::OnCancel, this, wxID_CANCEL);
//        sizer_buttons->Add(closeButton, 1, wxLEFT, 2);
//
//        vbox->Add(panel, 1, wxTOP | wxLEFT | wxRIGHT, 10);
//        vbox->Add(sizer_buttons, 0, wxALIGN_RIGHT | wxALL, 10);
    }

private:
    virtual void OnClose(wxCloseEvent &event) {
        this->Destroy();
    }

    virtual void OnCancel(wxCommandEvent &event) {
        this->Close();
    }

    virtual bool TransferDataFromWindow() {
        // TODO(allan): put in thread. Maybe use wxThread. Use wxQueueEvent to wake up GUI thread from SSH thread.
        auto sftpConnection = make_unique<SftpConnection>(this->user->GetValue().ToStdString(),
                                                          this->host->GetValue().ToStdString(), 22);
        wxFrame *frame = new SftpguiFrame(move(sftpConnection), config);
        frame->Show();

        this->Close();
        return true;
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

            if (!host.empty()) {
                // TODO(allan): put in thread. Maybe use wxThread. Use wxQueueEvent to wake up GUI thread from SSH thread.
                auto sftpConnection = make_unique<SftpConnection>(username, host, port);

                wxFrame *frame = new SftpguiFrame(move(sftpConnection), config);
                frame->Show();
            } else {
                auto dialog = new HostSelectionDialog(config);
                dialog->Show();
            }

        } catch (...) {
            showException();
            return false;
        }

        return true;
    }

    virtual void OnInitCmdLine(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        parser.SetSwitchChars(wxT("-"));
        parser.AddParam("[user@]host", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddSwitch("h", "help", "displays help", wxCMD_LINE_OPTION_HELP);
        parser.AddOption("p", "port", "remote SSH server port");
    }

    virtual bool OnCmdLineParsed(wxCmdLineParser &parser) {  // NOLINT: wxWidgets legacy
        if (parser.GetParamCount() > 0) {
            this->host = parser.GetParam(0);
        }

        this->username = wxGetUserId();
#ifdef WIN32
        transform(this->username.begin(), this->username.end(), this->username.begin(), ::tolower);
#endif

        if (this->host.find("@") != string::npos) {
            int i = this->host.find("@");
            this->username = this->host.substr(0, i);
            this->host = this->host.substr(i + 1);
        }

        wxString p;
        if (parser.Found("p", &p)) {
            string ps = string(p);
            if (!std::all_of(ps.begin(), ps.end(), ::isdigit)) {
                wxLogFatalError("non-digit port number");
                return false;
            }
            this->port = stoi(string(p));
            if (!(0 < this->port && this->port < 65536)) {
                wxLogFatalError("invalid port number");
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
