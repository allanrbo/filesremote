// Copyright 2020 Allan Riordan Boll

#ifndef SRC_DIRENTRY_H_
#define SRC_DIRENTRY_H_

#include <libssh2_sftp.h>

#include <string>

using std::string;

class DirEntry {
public:
    string name_;
    uint64_t size_ = 0;
    uint64_t modified_ = 0;
    uint64_t mode_;
    string mode_str_;
    string owner_;
    string group_;
    bool is_dir_;

    DirEntry() {}

    explicit DirEntry(LIBSSH2_SFTP_ATTRIBUTES attrs);

    string SizeFormatted(bool as_bytes);

    string ModifiedFormatted();
};

#endif  // SRC_DIRENTRY_H_
