// Copyright 2020 Allan Riordan Boll

#include "src/direntry.h"

#include <wx/wx.h>

#include <libssh2_sftp.h>

#include <iomanip>
#include <sstream>
#include <string>

using std::string;
using std::stringstream;
using std::to_string;


static string Rounded(double d, int precision) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << d;
    return ss.str();
}


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

    if (this->size_ < 1024) {
        return to_string(this->size_) + " bytes";
    }

    double size = this->size_;
    size = size / 1024.0;
    if (size < 1024) {
        return Rounded(size, 1) + " KiB";
    }

    size = size / 1024.0;
    if (size < 1024) {
        return Rounded(size, 2) + " MiB";
    }

    size = size / 1024.0;
    if (size < 1024) {
        return Rounded(size, 2) + " GiB";
    }

    size = size / 1024.0;
    return Rounded(size, 2) + " TiB";
}

string DirEntry::ModifiedFormatted() {
    if (this->modified_ < 5) {
        return "";
    }
    auto t = wxDateTime((time_t) this->modified_);
    t.MakeUTC();
    return t.FormatISOCombined(' ').ToStdString(wxMBConvUTF8());
}
