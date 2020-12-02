// Copyright 2020 Allan Riordan Boll

#ifndef SRC_DIRLISTCTRL_H_
#define SRC_DIRLISTCTRL_H_

#ifdef __WXMSW__
#include <winsock2.h>  // Several header files include windows.h, but winsock2.h needs to come first.
#endif

#include <wx/config.h>
#include <wx/dataview.h>
#include <wx/listctrl.h>
#include <wx/wx.h>

#include <future>  // NOLINT
#include <vector>

#include "src/direntry.h"

using std::function;
using std::vector;

typedef function<void(void)> OnItemActivatedCb;
typedef function<void(int)> OnColumnHeaderClickCb;

// A base class, because wxDataViewListCtrl looks best on MacOS, and wxListCtrl looks best on GTK and Windows.
class DirListCtrl {
protected:
    OnItemActivatedCb on_item_activated_cb_;
    OnColumnHeaderClickCb on_column_header_click_cb_;
    wxImageList *icons_image_list_;

    int IconIdx(DirEntry entry);

public:
    explicit DirListCtrl(wxImageList *icons_image_list) : icons_image_list_(icons_image_list) {
    }

    virtual void Refresh(vector<DirEntry> entries) = 0;

    virtual wxControl *GetCtrl() = 0;

    virtual void SetFocus() = 0;

    virtual void ActivateCurrent() = 0;

    virtual vector<int> GetSelected() = 0;

    virtual void SetSelected(vector<int>) = 0;

    virtual int GetHighlighted() = 0;

    virtual void SetHighlighted(int) = 0;

    void BindOnItemActivated(OnItemActivatedCb cb) {
        this->on_item_activated_cb_ = cb;
    }

    void BindOnColumnHeaderClickCb(OnColumnHeaderClickCb cb) {
        this->on_column_header_click_cb_ = cb;
    }
};

class DvlcDirList : public DirListCtrl {
    wxDataViewListCtrl *dvlc_;
    wxConfigBase *config_;

public:
    explicit DvlcDirList(wxWindow *parent, wxConfigBase *config, wxImageList *icons_image_list);

    void Refresh(vector<DirEntry> entries);

    wxControl *GetCtrl();

    void SetFocus();

    void ActivateCurrent();

    vector<int> GetSelected();

    void SetSelected(vector<int>);

    int GetHighlighted();

    void SetHighlighted(int);
};

class LcDirList : public DirListCtrl {
    wxListCtrl *list_ctrl_;
    wxConfigBase *config_;

public:
    explicit LcDirList(wxWindow *parent, wxConfigBase *config, wxImageList *icons_image_list);

    void Refresh(vector<DirEntry> entries);

    wxControl *GetCtrl();

    void SetFocus();

    void ActivateCurrent();

    vector<int> GetSelected();

    void SetSelected(vector<int>);

    int GetHighlighted();

    void SetHighlighted(int);
};

#endif  // SRC_DIRLISTCTRL_H_
