// Copyright 2020 Allan Riordan Boll

#include "src/dirlistctrl.h"

#ifdef __WXMSW__
#include <winsock2.h>  // Several header files include windows.h, but winsock2.h needs to come first.
#endif

#include <wx/config.h>
#include <wx/dataview.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/wx.h>

#include <future>  // NOLINT
#include <regex>  // NOLINT
#include <vector>

#include "src/direntry.h"

using std::function;
using std::regex;
using std::vector;


typedef function<void(void)> OnItemActivatedCb;
typedef function<void(int)> OnColumnHeaderClickCb;


int DirListCtrl::IconIdx(DirEntry entry) {
    // These numbers correspond to the order the icons in icons_image_list_ were added...
    int r = 0;
    if (entry.is_dir_) {
        r = 1;
    } else if (LIBSSH2_SFTP_S_ISLNK(entry.mode_)) {
        r = 3;
    } else if (entry.mode_ & LIBSSH2_SFTP_S_IXUSR || entry.mode_ & LIBSSH2_SFTP_S_IXGRP
               || entry.mode_ & LIBSSH2_SFTP_S_IXOTH) {
        r = 2;
    } else if (regex_search(entry.name_, regex(
            "(\\.jpeg|\\.jpg|\\.png|\\.gif|\\.webp|\\.bmp|\\.psd|\\.ai|\\.svg|\\.psd|\\.eps|\\.tif|\\.tiff)$"))) {
        r = 4;
    } else if (regex_search(entry.name_, regex("(\\.tar|\\.tgz|\\.gz|\\.bz2|\\.7z|\\.xz|\\.zip)$"))) {
        r = 5;
    }
    return r;
}

DvlcDirList::DvlcDirList(wxWindow *parent, wxConfigBase *config, wxImageList *icons_image_list) : DirListCtrl(
        icons_image_list) {
    this->dvlc_ = new wxDataViewListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxDV_ROW_LINES);
    this->config_ = config;

    // TODO(allan): wxDATAVIEW_CELL_EDITABLE for renaming files?
    auto col = this->dvlc_->AppendIconTextColumn("  Name", wxDATAVIEW_CELL_INERT);
    col->SetWidth(300);

    col = this->dvlc_->AppendTextColumn(" Size", wxDATAVIEW_CELL_INERT);
    col->SetWidth(100);

    col = this->dvlc_->AppendTextColumn(" Modified", wxDATAVIEW_CELL_INERT);
    col->SetWidth(150);

    col = this->dvlc_->AppendTextColumn(" Mode", wxDATAVIEW_CELL_INERT);
    col->SetWidth(100);

    col = this->dvlc_->AppendTextColumn(" Owner", wxDATAVIEW_CELL_INERT, 100);
    col->SetWidth(100);

    col = this->dvlc_->AppendTextColumn(" Group", wxDATAVIEW_CELL_INERT, 100);
    col->SetWidth(100);

    this->dvlc_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [&](wxDataViewEvent &evt) {
        if (!evt.GetItem()) {
            return;
        }
        this->on_item_activated_cb_();
    });

    this->dvlc_->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, [&](wxDataViewEvent &evt) {
        this->on_column_header_click_cb_(evt.GetColumn());
    });
}

void DvlcDirList::Refresh(vector<DirEntry> entries) {
    bool as_bytes = false;
    if (this->config_->Read("/size_units", "1") == "2") {
        as_bytes = true;
    }

    this->dvlc_->DeleteAllItems();

    for (int i = 0; i < entries.size(); i++) {
        wxIcon icon = this->icons_image_list_->GetIcon(this->IconIdx(entries[i]));

        wxVector<wxVariant> data;
        data.push_back(wxVariant(wxDataViewIconText(wxString::FromUTF8(entries[i].name_), icon)));
        data.push_back(wxVariant(entries[i].SizeFormatted(as_bytes)));
        data.push_back(wxVariant(entries[i].ModifiedFormatted()));
        data.push_back(wxVariant(entries[i].mode_str_));
        data.push_back(wxVariant(entries[i].owner_));
        data.push_back(wxVariant(entries[i].group_));
        this->dvlc_->AppendItem(data, i);
    }
}

wxControl *DvlcDirList::GetCtrl() {
    return this->dvlc_;
}

void DvlcDirList::SetFocus() {
    this->dvlc_->SetFocus();
}

void DvlcDirList::ActivateCurrent() {
    if (this->dvlc_->GetCurrentItem()) {
        this->on_item_activated_cb_();
    }
}

vector<int> DvlcDirList::GetSelected() {
    vector<int> r;
    for (int i = 0; i < this->dvlc_->GetItemCount(); ++i) {
        if (this->dvlc_->IsRowSelected(i)) {
            r.push_back(i);
        }
    }
    return r;
}

void DvlcDirList::SetSelected(vector<int> selected) {
    wxDataViewItemArray a;
    for (int i = 0; i < selected.size(); ++i) {
        a.push_back(this->dvlc_->RowToItem(selected[i]));
    }
    this->dvlc_->SetSelections(a);
}

int DvlcDirList::GetHighlighted() {
    int i = this->dvlc_->ItemToRow(this->dvlc_->GetCurrentItem());
    if (i < 0) {
        return 0;
    }
    return i;
}

void DvlcDirList::SetHighlighted(int row) {
    auto item = this->dvlc_->RowToItem(row);
    if (item.IsOk()) {
        this->dvlc_->SetCurrentItem(item);
        this->dvlc_->EnsureVisible(item);
    }
}


LcDirList::LcDirList(wxWindow *parent, wxConfigBase *config, wxImageList *icons_image_list) : DirListCtrl(
        icons_image_list) {
    this->list_ctrl_ = new wxListCtrl(
            parent,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            wxLC_REPORT | wxLC_SINGLE_SEL);
    this->config_ = config;

    this->list_ctrl_->AssignImageList(this->icons_image_list_, wxIMAGE_LIST_SMALL);

    this->list_ctrl_->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
    this->list_ctrl_->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, 100);
    this->list_ctrl_->InsertColumn(2, "Modified", wxLIST_FORMAT_LEFT, 150);
    this->list_ctrl_->InsertColumn(3, "Mode", wxLIST_FORMAT_LEFT, 100);
    this->list_ctrl_->InsertColumn(4, "Owner", wxLIST_FORMAT_LEFT, 100);
    this->list_ctrl_->InsertColumn(5, "Group", wxLIST_FORMAT_LEFT, 100);

    this->list_ctrl_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [&](wxListEvent &evt) {
        this->on_item_activated_cb_();
    });

    this->list_ctrl_->Bind(wxEVT_LIST_COL_CLICK, [&](wxListEvent &evt) {
        this->on_column_header_click_cb_(evt.GetColumn());
    });
}

wxControl *LcDirList::GetCtrl() {
    return this->list_ctrl_;
}

void LcDirList::Refresh(vector<DirEntry> entries) {
    bool as_bytes = false;
    if (this->config_->Read("/size_units", "1") == "2") {
        as_bytes = true;
    }

    this->list_ctrl_->DeleteAllItems();

    for (int i = 0; i < entries.size(); i++) {
        this->list_ctrl_->InsertItem(i, entries[i].name_, this->IconIdx(entries[i]));
        this->list_ctrl_->SetItemData(i, i);
        this->list_ctrl_->SetItem(i, 0, wxString::FromUTF8(entries[i].name_));
        this->list_ctrl_->SetItem(i, 1, entries[i].SizeFormatted(as_bytes));
        this->list_ctrl_->SetItem(i, 2, entries[i].ModifiedFormatted());
        this->list_ctrl_->SetItem(i, 3, entries[i].mode_str_);
        this->list_ctrl_->SetItem(i, 4, entries[i].owner_);
        this->list_ctrl_->SetItem(i, 5, entries[i].group_);
    }
}

void LcDirList::SetFocus() {
    this->list_ctrl_->SetFocus();
}

void LcDirList::ActivateCurrent() {
    if (this->list_ctrl_->GetSelectedItemCount() > 0) {
        this->on_item_activated_cb_();
    }
}

void LcDirList::SetSelected(vector<int> selected) {
    for (int i = 0; i < selected.size(); ++i) {
        this->list_ctrl_->SetItemState(selected[i], wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
}

vector<int> LcDirList::GetSelected() {
    vector<int> r;
    int64_t cur = -1;
    while (1) {
        cur = this->list_ctrl_->GetNextItem(cur, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (cur == -1) {
            break;
        }

        r.push_back(cur);
    }
    return r;
}

int LcDirList::GetHighlighted() {
    auto i = this->list_ctrl_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
    if (i < 0) {
        return 0;
    }
    int j = this->list_ctrl_->GetItemData(i);
    return j;
}

void LcDirList::SetHighlighted(int row) {
    if (row >= this->list_ctrl_->GetItemCount()) {
        return;
    }

    this->list_ctrl_->SetItemState(row, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
    if (row != 0) {
        this->list_ctrl_->EnsureVisible(row);
    }
}
