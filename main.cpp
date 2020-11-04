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

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/cmdline.h>
#include <wx/utils.h>

#include <wx/dataview.h>   // TODO used?

#include <libssh2.h>
#include <libssh2_sftp.h>


using namespace std;


class DirEntry {
public:
    string name;
    uint64_t size = 0;
    unsigned long modified = 0;
    unsigned long permissions;
    string owner;
    string group;
};

class SftpConnection {
    LIBSSH2_SFTP_HANDLE *sftp_opendir_handle = NULL;
    LIBSSH2_SFTP *sftp_session = NULL;
    LIBSSH2_SESSION *session = NULL;
    int sock = 0;

public:
    SftpConnection(string username, string host) {
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
        sin.sin_port = htons(22);
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
    }

    vector<DirEntry> getDir(string path) {
        int rc;

        this->sftp_opendir_handle = libssh2_sftp_opendir(this->sftp_session, path.c_str());
        if (!this->sftp_opendir_handle) {
            throw runtime_error("libssh2_sftp_opendir failed");
        }

        auto files = vector<DirEntry>();
        while (1) {
            char name[512];
            char line[512];
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            memset(name, 0, 512);
            memset(line, 0, 512);

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
            if(attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
                d.size = attrs.filesize;
            }
            if(attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
                d.modified = attrs.mtime;
            }
            if(attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
                d.permissions = attrs.permissions;
            }
            files.push_back(d);
        }

        if (this->sftp_opendir_handle) {
            libssh2_sftp_closedir(this->sftp_opendir_handle);
            this->sftp_opendir_handle = NULL;
        }

        return files;
    }

    int getfilecontent() {
        /*
        sftp_handle = libssh2_sftp_open(sftp_session, sftppath, LIBSSH2_FXF_READ, 0);
        if (!sftp_handle) {
            fprintf(stderr, "Unable to open file with SFTP: %ld\n", libssh2_sftp_last_error(sftp_session));
            fprintf(stderr, "libssh2_sftp_open()!\n");
            return -1; // TODO shutdown
        }

        do {
            char mem[1024];

            rc = libssh2_sftp_read(sftp_handle, mem, sizeof(mem));
            if (rc > 0) {
                write(1, mem, rc);
            } else {
                break;
            }
        } while (1);
        libssh2_sftp_close(sftp_handle);
        */
        return 0;
    }


    ~SftpConnection() {
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


class MyFrame : public wxFrame {
    wxListCtrl *dir_list_ctrl;
    wxDataViewListCtrl *dir_dvlc;
    wxTextCtrl *path_text_ctrl;
    unique_ptr<SftpConnection> sftpConnection;
    string current_dir = "/";

public:
    MyFrame(unique_ptr<SftpConnection> sftpConnection) : wxFrame(
            NULL,
            wxID_ANY,
            wxT("Sftpgui"),
            wxPoint(-1, -1),
            wxSize(800, 600)
    ) {
        this->sftpConnection = move(sftpConnection);

        wxPanel *mainPane = new wxPanel(this);

        this->path_text_ctrl = new wxTextCtrl(mainPane, wxID_ANY, this->current_dir, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        this->path_text_ctrl->Bind(wxEVT_TEXT_ENTER, &MyFrame::onPathTextEnter, this);

        this->dir_list_ctrl = new wxListCtrl(mainPane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
        this->dir_list_ctrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &MyFrame::onItemActivated, this);

        auto ap = wxArtProvider();
        auto size = wxSize(16, 16);
        wxImageList *il = new wxImageList(size.GetWidth(), size.GetHeight(), false, 1);
        il->Add(ap.GetBitmap(wxART_NORMAL_FILE, wxART_LIST, size));
        il->Add(ap.GetBitmap(wxART_FOLDER, wxART_LIST, size));
        this->dir_list_ctrl->AssignImageList(il, wxIMAGE_LIST_SMALL);

        this->dir_list_ctrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
        this->dir_list_ctrl->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, 100);
        this->dir_list_ctrl->InsertColumn(2, "Modified", wxLIST_FORMAT_LEFT, 100);
        this->dir_list_ctrl->InsertColumn(3, "Permissions", wxLIST_FORMAT_LEFT, 100);
        this->dir_list_ctrl->InsertColumn(4, "Owner", wxLIST_FORMAT_LEFT, 100);





        this->dir_dvlc = new wxDataViewListCtrl(mainPane, wxID_ANY , wxDefaultPosition, wxDefaultSize, wxDV_MULTIPLE | wxDV_ROW_LINES);
        this->dir_dvlc->AppendIconTextColumn("Name" , wxDATAVIEW_CELL_EDITABLE, 300);
        this->dir_dvlc->AppendTextColumn("Size" , wxDATAVIEW_CELL_INERT, 100);
        this->dir_dvlc->AppendTextColumn("Modified" , wxDATAVIEW_CELL_INERT, 100);
        this->dir_dvlc->AppendTextColumn("Permissions" , wxDATAVIEW_CELL_INERT, 100);
        this->dir_dvlc->AppendTextColumn("Owner" , wxDATAVIEW_CELL_INERT, 100);


        wxBoxSizer *sizer_top = new wxBoxSizer(wxHORIZONTAL);
        sizer_top->Add(this->path_text_ctrl, 1, wxEXPAND | wxALL, 1);

        wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(sizer_top, 0, wxEXPAND | wxALL, 1);
        sizer->Add(this->dir_list_ctrl, 1, wxEXPAND | wxALL, 0);
        sizer->Add(dir_dvlc, 1, wxEXPAND | wxALL, 0);
        mainPane->SetSizer(sizer);


        this->refresh_dir();

        this->dir_list_ctrl->SetFocus();
    }

private:

    void onPathTextEnter(wxCommandEvent& event) {
        this->current_dir = this->path_text_ctrl->GetValue();
        this->refresh_dir();
    }

    void onItemActivated(wxListEvent& event) {

        int n = this->dir_list_ctrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        string s = string(this->dir_list_ctrl->GetItemText(n));
        this->current_dir += "/" + s;
        this->path_text_ctrl->SetValue(this->current_dir);
        this->refresh_dir();
    }

    void refresh_dir() {
        // TODO make into a field
        auto ap = wxArtProvider();
        auto size = wxSize(16, 16);

        this->dir_dvlc->DeleteAllItems();

        this->dir_list_ctrl->DeleteAllItems();

        vector<DirEntry> l = this->sftpConnection->getDir(this->current_dir);

        for (int n = 0; n < l.size(); n++) {
            int iconIdx = 0;
            if (l[n].permissions & S_IFDIR) {
                iconIdx = 1;
            }

            this->dir_list_ctrl->InsertItem(n, l[n].name, iconIdx);
            this->dir_list_ctrl->SetItemData(n, n);
            this->dir_list_ctrl->SetItem(n, 0, l[n].name);
            this->dir_list_ctrl->SetItem(n, 1, to_string(l[n].size));
            this->dir_list_ctrl->SetItem(n, 2, to_string(l[n].modified));
            this->dir_list_ctrl->SetItem(n, 3, to_string(l[n].permissions));
            this->dir_list_ctrl->SetItem(n, 4, l[n].owner);


            wxIcon icon;
            if (l[n].permissions & S_IFDIR) {
                icon = ap.GetIcon(wxART_FOLDER , wxART_LIST, size);
            } else {
                icon = ap.GetIcon(wxART_NORMAL_FILE , wxART_LIST, size);
            }

            wxVector<wxVariant> data;
            data.push_back(wxVariant(wxDataViewIconText(l[n].name, icon)));
            data.push_back( wxVariant(to_string(l[n].size)));
            data.push_back( wxVariant(to_string(l[n].modified)));
            data.push_back( wxVariant(to_string(l[n].permissions)));
            data.push_back( wxVariant(l[n].owner));
            dir_dvlc->AppendItem(data );
        }
    }
};

class MyApp : public wxApp {
    string host;
    string username;

public:

    bool OnInit() {
        if (!wxApp::OnInit())
            return false;

        try {
            auto sftpConnection = make_unique<SftpConnection>(username, host);
            wxFrame *m_frame = new MyFrame(move(sftpConnection));
            m_frame->Show();
        } catch (runtime_error e) {
            fprintf(stderr, "%s\n", e.what());
            return false;
        }

        return true;
    }

    void OnInitCmdLine(wxCmdLineParser &parser) {
        parser.SetSwitchChars(wxT("-"));
        parser.AddParam("[user@]host");
        parser.AddSwitch("h", "help", "displays help", wxCMD_LINE_OPTION_HELP);
    }

    bool OnCmdLineParsed(wxCmdLineParser &parser) {
        this->host = parser.GetParam(0);
        this->username = wxGetUserId();
#ifdef WIN32
        transform(this->username.begin(), this->username.end(), this->username.begin(), ::tolower);
#endif
        return true;
    }
};

IMPLEMENT_APP(MyApp)



//#include <wx/wx.h>
//#include <wx/dataview.h>
//#include <wx/hashset.h>
//enum Identifiers {
//    IDTREEAPP = wxID_HIGHEST + 1,
//    IDDATAVIEW = wxID_HIGHEST + 2,
//    IDREDO = wxID_HIGHEST + 3
//};
//WX_DECLARE_HASH_SET(wxString, wxStringHash, wxStringEqual, StringHash);
//StringHash strings;
//wxStringCharType* asPointer(const wxString s) {
//    if (s.length() < 2) {
//        return NULL;
//    }
//    strings.insert(s);
//    StringHash::iterator it = strings.find(s);
//    assert(it != strings.end());
//    wxStringCharType* res = const_cast<wxStringCharType* >(it->wx_str());
//    return res;
//}
//class TestModel: public wxDataViewModel {
//public:
//    TestModel(): wxDataViewModel(), topic(0) {}
//    ~TestModel() {}
//    unsigned int GetColumnCount() const {
//        return 2;
//    }
//    wxString GetColumnType(unsigned int column) const {
//        return "string";
//    }
//    void GetValue(wxVariant& val, const wxDataViewItem& item, unsigned int column) const {
//        wxVariant v(_asString(item));
//        val = v;
//    }
//    bool SetValue(const wxVariant& val, const wxDataViewItem& item, unsigned int column) {
//        return true;
//    }
//    wxDataViewItem GetParent(const wxDataViewItem& item) const {
//        wxString par = _asString(item);
//        if (par.length() != 1) {
//            return wxDataViewItem(asPointer(par.Left(par.length() - 1)));
//        }
//        return wxDataViewItem(NULL);
//    }
//    bool IsContainer(const wxDataViewItem& item) const {
//        wxString par = _asString(item);
//        return (par.length() < 3);
//    }
//    unsigned GetChildren(const wxDataViewItem& item, wxDataViewItemArray& children) const {
//        return 0;
//
//        wxString par = _asString(item);
//        if (topic == 0 or par.length() == 3) {
//            return 0;
//        }
//        children.Add(wxDataViewItem(asPointer(par + "a")));
//        children.Add(wxDataViewItem(asPointer(par + "b")));
//        children.Add(wxDataViewItem(asPointer(par + "c")));
//        return 3;
//    }
//    void setTopic(int ptopic) {
//        topic = ptopic;
//        if (topic > 1) {
//            Cleared();
//        }
//    }
//    int topic;
//private:
//    wxString _asString(const wxDataViewItem& item) const {
//        if (item.IsOk()) {
//            return static_cast<wxStringCharType*>(item.GetID());
//        }
//        wxStringCharType ch = 'A' + topic;
//        return wxString(ch);
//    }
//};
//class TreeFrame: public wxFrame {
//public:
//    TreeFrame(const wxString& title, const wxPoint& pos, const wxSize& size):
//            wxFrame(NULL, IDTREEAPP, title, pos, size) {
//        _sizer = new wxFlexGridSizer(1, 3, 3);
//        dataView = new wxDataViewCtrl(this, IDDATAVIEW, wxDefaultPosition, wxSize(300,300),  wxDV_MULTIPLE | wxDV_ROW_LINES);
//        wxDataViewTextRenderer* rend0 = new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_EDITABLE);
//
//        _column0 = new wxDataViewColumn("col1", rend0, 0, 100, wxAlignment(wxALIGN_LEFT | wxALIGN_TOP), wxDATAVIEW_COL_RESIZABLE);
//        dataView->AppendColumn(_column0);
//        dataView->SetExpanderColumn(_column0);
//
//
//        wxDataViewColumn* _column2 = new wxDataViewColumn("col2", rend0, 0, 100, wxAlignment(wxALIGN_LEFT | wxALIGN_TOP), wxDATAVIEW_COL_RESIZABLE);
//        dataView->AppendColumn(_column2);
//        dataView->SetExpanderColumn(_column2);
//
//
//        _sizer->Add(dataView);
//        _redoButton = new wxButton(this, IDREDO, "Redo");
//        _sizer->Add(_redoButton);
//        SetSizerAndFit(_sizer);
//    }
//    void onRedo(wxCommandEvent& event) {
//        testModel->setTopic(testModel->topic + 1);
//    }
//    TestModel* testModel;
//    wxDataViewCtrl* dataView;
//private:
//    wxFlexGridSizer* _sizer;
//    wxDataViewColumn* _column0;
//    wxButton* _redoButton;
//DECLARE_EVENT_TABLE()
//};
//BEGIN_EVENT_TABLE(TreeFrame, wxFrame)
//                EVT_BUTTON(IDREDO, TreeFrame::onRedo)
//END_EVENT_TABLE()
//class TreeApp: public wxApp {
//public:
//    virtual bool OnInit() {
//        if (!wxApp::OnInit()) {
//            return false;
//        }
//        _treeFrame = new TreeFrame("Test tree frame", wxPoint(50, 50), wxSize(300, 300));
//        SetTopWindow(_treeFrame);
//        _treeFrame->testModel = new TestModel;
//        _treeFrame->dataView->AssociateModel(_treeFrame->testModel);
//        _treeFrame->testModel->setTopic(1);
//        _treeFrame->Show(true);
//        return true;
//    }
//private:
//    TreeFrame* _treeFrame;
//};
//IMPLEMENT_APP(TreeApp)