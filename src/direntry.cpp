// Copyright 2020 Allan Riordan Boll

#include "src/direntry.h"

#include <wx/wx.h>

#include <libssh2_sftp.h>

#include <string>

#include "src/storageunits.h"

using std::to_string;

DirEntry::DirEntry(LIBSSH2_SFTP_ATTRIBUTES attrs) {
    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
        this->size_ = attrs.filesize;
    }
    if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        this->modified_ = attrs.mtime;
    }
    if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        this->mode_ = attrs.permissions;
        this->is_dir_ = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
    }
}

string DirEntry::SizeFormatted(bool as_bytes) {
    if (this->is_dir_) {
        return "";
    }

    if (as_bytes) {
        return to_string(this->size_);
    }
    return size_string(this->size_);
}

string DirEntry::ModifiedFormatted() {
    if (this->modified_ < 5) {
        return "";
    }
    auto t = wxDateTime((time_t) this->modified_);
    t.MakeUTC();
    return t.FormatISOCombined(' ').ToStdString(wxMBConvUTF8());
}
