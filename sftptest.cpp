#include <libssh2.h>
#include <libssh2_sftp.h>
#include <stdio.h>
#include <string>
#include <vector>

#ifdef _WIN32

#include <winsock2.h>

#else

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

#endif

using namespace std;

int getfiles(vector<string> &files) {
    const char *username = "allan";
    const char *sftppath = "/home/allan/hello.txt";

    LIBSSH2_SFTP_HANDLE *sftp_handle;

    struct libssh2_agent_publickey *identity, *prev_identity = NULL;

#ifdef WIN32
    WSADATA wsadata;
    int err = WSAStartup(MAKEWORD(2, 0), &wsadata);
    if(err != 0) {
        fprintf(stderr, "WSAStartup failed with error: %d\n", err);
        return 1;
    }
#endif

    unsigned long hostaddr = inet_addr("192.168.50.228");

    int rc = libssh2_init(0);
    if (rc != 0) {
        fprintf(stderr, "libssh2 initialization failed (%d)\n", rc);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(22);
    sin.sin_addr.s_addr = hostaddr;
    if (connect(sock, (struct sockaddr *) (&sin), sizeof(struct sockaddr_in)) != 0) {
        fprintf(stderr, "failed to connect!\n");
        return -1;
    }

    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session)
        return -1;

    libssh2_session_set_blocking(session, 1);

    rc = libssh2_session_handshake(session, sock);
    if (rc) {
        fprintf(stderr, "Failure establishing SSH session: %d\n", rc);
        return -1;
    }

    // TODO: fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA1);

    char *userauthlist = libssh2_userauth_list(session, username, strlen(username));

    LIBSSH2_AGENT *agent = libssh2_agent_init(session);
    if (!agent) {
        fprintf(stderr, "Failure initializing ssh-agent support\n");
        return -1; // TODO shutdown
    }
    if (libssh2_agent_connect(agent)) {
        fprintf(stderr, "Failure connecting to ssh-agent\n");
        return -1; // TODO shutdown
    }
    if (libssh2_agent_list_identities(agent)) {
        fprintf(stderr, "Failure requesting identities to ssh-agent\n");
        return -1; // TODO shutdown
    }
    while (1) {
        rc = libssh2_agent_get_identity(agent, &identity, prev_identity);
        if (rc == 1) {
            break;
        }

        if (rc < 0) {
            fprintf(stderr, "Failure obtaining identity from ssh-agent support\n");
            return -1; // TODO shutdown
        }

        if (libssh2_agent_userauth(agent, username, identity)) {
            fprintf(stderr, "Authentication with username %s and public key %s failed!\n", username, identity->comment);
        } else {
            fprintf(stderr, "Authentication with username %s and public key %s succeeded\n", username,
                    identity->comment);
            break;
        }

        prev_identity = identity;
    }
    if (rc) {
        fprintf(stderr, "Couldn't continue authentication\n");
        return -1; // TODO shutdown
    }


    LIBSSH2_SFTP *sftp_session = libssh2_sftp_init(session);
    if (!sftp_session) {
        fprintf(stderr, "Unable to init SFTP session\n");
        return -1; // TODO shutdown
    }

    sftp_handle = libssh2_sftp_opendir(sftp_session, "/home/allan");

    if(!sftp_handle) {
        fprintf(stderr, "Unable to open dir with SFTP\n");
        return -1; // TODO shutdown
    }
    fprintf(stderr, "libssh2_sftp_opendir() is done, now receive listing!\n");
    do {
        char mem[512];
        char longentry[512];
        LIBSSH2_SFTP_ATTRIBUTES attrs;

        rc = libssh2_sftp_readdir_ex(sftp_handle, mem, sizeof(mem), longentry, sizeof(longentry), &attrs);
        if(rc > 0) {
            if(longentry[0] != '\0') {
                printf("%s\n", longentry);
                files.push_back(longentry);
            }
            else {
                if(attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
                    /* this should check what permissions it
                       is and print the output accordingly */
                    printf("--fix----- ");
                }
                else {
                    printf("---------- ");
                }

                if(attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID) {
                    printf("%4d %4d ", (int) attrs.uid, (int) attrs.gid);
                }
                else {
                    printf("   -    - ");
                }

                if(attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
                    printf("%8llu ", attrs.filesize);
                    // TODO maybe "I64" instead of "llu" on WIN32...
                }

                printf("%s\n", mem);

                //files.push_back(string(longentry));
            }
        }
        else
            break;

    } while(1);

    libssh2_sftp_closedir(sftp_handle);


    libssh2_sftp_shutdown(sftp_session);

    // Shutdown
    libssh2_session_disconnect(session, "Normal Shutdown");
    libssh2_session_free(session);
#ifdef WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    libssh2_exit();

    return 0;
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

int main(int argc, char *argv[]) {
    vector <string> l;
    getfiles(l);

    printf("%d\n", l.size());

    for (auto &s : l) {
        printf("%s\n", s.c_str());
    }
}