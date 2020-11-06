#ifdef _WIN32

#include <winsock2.h>

#else

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif

#include <stdio.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
#include <filesystem>
#include <sstream>

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


#include <wx/dataview.h>   // TODO used?

#include <libssh2.h>
#include <libssh2_sftp.h>

using namespace std;

#define BUFLEN 4096


void showException() {
    wxString error;
    try {
        throw; // Rethrow the current exception in order to pattern match it here.
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
    unsigned long modified = 0;
    unsigned long permissions;
    string owner;
    string group;
    bool isDir;
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
        if(rc != 0) {
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

            sin.sin_addr.s_addr = *(u_long *) remote_host->h_addr_list[0];
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

        // TODO: verify fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);
        //char *userauthlist = libssh2_userauth_list(this->session, username.c_str(), username.size());

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
            char name[BUFLEN];
            char line[BUFLEN];
            LIBSSH2_SFTP_ATTRIBUTES attrs;
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
                d.permissions = attrs.permissions;
                d.isDir = attrs.permissions & S_IFDIR;
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
                // TODO error handling for fwrite.
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
                LIBSSH2_FXF_WRITE,
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
                // TODO error handling for fread.
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
    DvlcDirList(wxWindow *parent) : DirListCtrl() {
        this->dvlc = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxDV_MULTIPLE | wxDV_ROW_LINES);
        // TODO wxDATAVIEW_CELL_EDITABLE?
        this->dvlc->AppendIconTextColumn("Name", wxDATAVIEW_CELL_INERT, 300);
        this->dvlc->AppendTextColumn("Size", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Modified", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Permissions", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->AppendTextColumn("Owner", wxDATAVIEW_CELL_INERT, 100);
        this->dvlc->wxDataViewListCtrl::Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &DvlcDirList::onItemActivated, this);
        this->dvlc->SetFocus();
    }

    void onItemActivated(wxDataViewEvent &event) {
        int i = this->dvlc->GetItemData(event.GetItem());
        this->onItemActivatedCb(i);
    }

    void Refresh(vector<DirEntry> entries) {
        // TODO make into a field. Or actually make the individual icons into fields
        auto ap = wxArtProvider();
        auto size = wxSize(16, 16);

        this->dvlc->DeleteAllItems();

        for (int i = 0; i < entries.size(); i++) {
            wxIcon icon = this->iconsImageList->GetIcon(this->iconIdx(entries[i]));

            wxVector<wxVariant> data;
            data.push_back(wxVariant(wxDataViewIconText(entries[i].name, icon)));
            data.push_back(wxVariant(to_string(entries[i].size)));
            data.push_back(wxVariant(to_string(entries[i].modified)));
            data.push_back(wxVariant(to_string(entries[i].permissions)));
            data.push_back(wxVariant(entries[i].owner));
            this->dvlc->AppendItem(data, i);
        }
    };

    wxControl *GetCtrl() {
        return this->dvlc;
    }
};


class LcDirList : public DirListCtrl {
    wxListCtrl *list_ctrl;

public:
    LcDirList(wxWindow *parent) : DirListCtrl() {
        this->list_ctrl = new wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);

        this->list_ctrl->AssignImageList(this->iconsImageList, wxIMAGE_LIST_SMALL);

        this->list_ctrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
        this->list_ctrl->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(2, "Modified", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(3, "Permissions", wxLIST_FORMAT_LEFT, 100);
        this->list_ctrl->InsertColumn(4, "Owner", wxLIST_FORMAT_LEFT, 100);

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
            this->list_ctrl->SetItem(i, 2, to_string(entries[i].modified));
            this->list_ctrl->SetItem(i, 3, to_string(entries[i].permissions));
            this->list_ctrl->SetItem(i, 4, entries[i].owner);
        }
    };

    void onItemActivated(wxListEvent &event) {
        int i = this->list_ctrl->GetItemData(event.GetItem());
        this->onItemActivatedCb(i);
    }
};

string normalize_path(string path) {
    // TODO support UTF-8 paths...
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

    void onPreferenceChanged(wxCommandEvent &event) {
        if (wxPreferencesEditor::ShouldApplyChangesImmediately()) {
            this->TransferDataFromWindow();
        }
    }

    virtual bool TransferDataToWindow() wxOVERRIDE {
        this->text_editor->SetValue(this->config->Read("/editor", ""));
        return true;
    }

    virtual bool TransferDataFromWindow() wxOVERRIDE {
        this->config->Write("/editor", this->text_editor->GetValue());
        this->config->Flush();
        return true;
    }
};

class PreferencesPageGeneral : public wxStockPreferencesPage {
    wxConfigBase *config;

public:
    PreferencesPageGeneral(wxConfigBase *config) : wxStockPreferencesPage(Kind_General) {
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
    filesystem::file_time_type modified;
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

        int x = this->config->Read("/window_x", -1);
        int y = this->config->Read("/window_y", -1);
        int w = this->config->Read("/window_w", 800);
        int h = this->config->Read("/window_h", 600);
        this->Move(x, y);
        this->SetClientSize(w, h);

        this->SetTitle("Sftpgui - " + this->sftp_connection->username + "@" + this->sftp_connection->host +
                       " port " + to_string(this->sftp_connection->port));

        this->CreateStatusBar();

        wxMenuBar *menuBar = new wxMenuBar();
        wxMenu *fileMenu = new wxMenu;
        menuBar->Append(fileMenu, "&File");
        fileMenu->Append(wxID_PREFERENCES);
        this->Bind(wxEVT_MENU, &SftpguiFrame::onPreferencesMenuItem, this, wxID_PREFERENCES);
        fileMenu->Append(wxID_EXIT, "E&xit", "Quit this program");
        this->Bind(wxEVT_MENU, &SftpguiFrame::onQuitMenuItem, this, wxID_EXIT);
        SetMenuBar(menuBar);

        this->file_watcher_timer.Bind(wxEVT_TIMER, &SftpguiFrame::onFileWatcherTimer, this);
        this->file_watcher_timer.Start(1000);

        wxPanel *mainPane = new wxPanel(this);

        this->path_text_ctrl = new wxTextCtrl(
                mainPane,
                wxID_ANY,
                this->current_dir,
                wxDefaultPosition,
                wxDefaultSize,
                wxTE_PROCESS_ENTER | wxBORDER_NONE);
        this->path_text_ctrl->Bind(wxEVT_TEXT_ENTER, &SftpguiFrame::onPathTextEnter, this);

#ifdef __APPLE__
        // On MacOS wxDataViewListCtrl looks best.
        this->dir_list_ctrl = new DvlcDirList(mainPane);
#else
        // On GTK and Windows wxListCtrl looks best.
        this->dir_list_ctrl = new LcDirList(mainPane);
#endif

        // TODO exception handling in this lambda...
        this->dir_list_ctrl->BindOnItemActivated([&](int n) {
            string status = "";
            try {
                auto entry = this->current_dir_list[n];
                if (entry.isDir) {
                    this->current_dir += "/" + entry.name;
                    this->current_dir = normalize_path(this->current_dir);
                    this->path_text_ctrl->SetValue(this->current_dir);
                    this->refreshDir();
                    status = "Fetched dir at " + wxDateTime::Now().FormatISOCombined() + ".";
                } else {
                    string editor = string(this->config->Read("/editor", ""));
                    if (editor.empty()) {
                        string msg = "No text editor configured. Set one in Preferences.";
                        wxMessageBox(msg, "Notice", wxOK | wxICON_INFORMATION, this);
                        return;
                    }

                    string remote_path = normalize_path(this->current_dir + "/" + entry.name);
                    string local_tmp = string(wxStandardPaths::Get().GetTempDir());
                    string local_dir = normalize_path(local_tmp + "/sftpgui/" + this->current_dir);
                    string local_path = normalize_path(local_dir + "/" + entry.name);

                    // TODO restrict permissions
                    filesystem::create_directories(local_dir);

                    this->sftp_connection->downloadFile(remote_path, local_path);

                    bool previously_downloaded = false;
                    for (int i = 0; i < this->opened_files_local.size(); ++i) {
                        if (this->opened_files_local[i].remote_path == remote_path) {
                            this->opened_files_local[i].modified = filesystem::last_write_time(local_path);
                            previously_downloaded = true;
                            break;
                        }
                    }
                    if (!previously_downloaded) {
                        OpenedFile f;
                        f.local_path = local_path;
                        f.remote_path = remote_path;
                        f.modified = filesystem::last_write_time(local_path);
                        this->opened_files_local.push_back(f);
                    }

                    status = "Downloaded " + remote_path + " at " + wxDateTime::Now().FormatISOCombined() + ".";

                    wxExecute(editor + " " + local_path, wxEXEC_ASYNC);
                }
            } catch (...) {
                showException();
            }
            this->setIdleStatusText();
        });

        wxBoxSizer *sizer_top = new wxBoxSizer(wxHORIZONTAL);
        sizer_top->Add(this->path_text_ctrl, 1, wxEXPAND | wxALL, 2);

        wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(sizer_top, 0, wxEXPAND | wxALL, 1);
        sizer->Add(this->dir_list_ctrl->GetCtrl(), 1, wxEXPAND | wxALL, 0);
        mainPane->SetSizer(sizer);

        this->refreshDir();

        this->setIdleStatusText();
    }

    ~SftpguiFrame() {
        for (int i = 0; i < this->opened_files_local.size(); ++i) {
            filesystem::remove(this->opened_files_local[i].local_path);
        }

        // Save frame position.
        int x, y, w, h;
        GetClientSize(&w, &h);
        GetPosition(&x, &y);
        this->config->Write("/window_x", (long) x);
        this->config->Write("/window_y", (long) y);
        this->config->Write("/window_w", (long) w);
        this->config->Write("/window_h", (long) h);
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

    void onFileWatcherTimer(wxTimerEvent &event) {
        for (int i = 0; i < this->opened_files_local.size(); ++i) {
            OpenedFile f = this->opened_files_local[i];
            if (filesystem::last_write_time(f.local_path) > f.modified) {
                this->sftp_connection->uploadFile(f.local_path, f.remote_path);
                this->opened_files_local[i].modified = filesystem::last_write_time(f.local_path);

                this->setIdleStatusText(
                        string("Uploaded " + f.remote_path + " at " + wxDateTime::Now().FormatISOCombined() + "."));
            }
        }
    }

    void onPathTextEnter(wxCommandEvent &event) {
        this->current_dir = this->path_text_ctrl->GetValue();
        this->refreshDir();
    }

    void refreshDir() {
        this->current_dir_list = this->sftp_connection->getDir(this->current_dir);
        this->dir_list_ctrl->Refresh(this->current_dir_list);
    }

    void onPreferencesMenuItem(wxCommandEvent &event) {
        auto prefs_editor = new wxPreferencesEditor();
        prefs_editor->AddPage(new PreferencesPageGeneral(this->config));
        prefs_editor->Show(this);
    }

    void onQuitMenuItem(wxCommandEvent &event) {
        Close(true);
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

            // TODO Better control the config path. ".config/sftpgui"
            auto es = wxEmptyString;
            wxFileConfig *config = new wxFileConfig("sftpgui", es, es, es, wxCONFIG_USE_LOCAL_FILE);
            config->EnableAutoSave();
            config->SetRecordDefaults();
            wxConfigBase::Set(config);

            // TODO put in separate thread. Maybe use wxThread. Use wxQueueEvent to wake up GUI thread from SSH thread.
            auto sftpConnection = make_unique<SftpConnection>(username, host, port);

            wxFrame *m_frame = new SftpguiFrame(move(sftpConnection), config);
            m_frame->Show();
        } catch (...) {
            showException();
            return false;
        }

        return true;
    }

    void OnInitCmdLine(wxCmdLineParser &parser) {
        parser.SetSwitchChars(wxT("-"));
        parser.AddParam("[user@]host");
        parser.AddSwitch("h", "help", "displays help", wxCMD_LINE_OPTION_HELP);
        parser.AddOption("p", "port", "remote SSH server port");
    }

    bool OnCmdLineParsed(wxCmdLineParser &parser) {
        this->host = parser.GetParam(0);

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
