<p align="center">
  <img width="120" height="120" src="https://github.com/allanrbo/filesremote/blob/master/graphics/appicon/icon_1024x1024.png">
</p>
<h3 align="center">FilesRemote</h3>
<p align="center">A local SSH file manager</p>
<p align="center">
  <img src="https://img.shields.io/github/v/release/allanrbo/filesremote?label=version" />
  <img src="https://img.shields.io/github/downloads/allanrbo/filesremote/total" />
</p>
<hr />

<p align="center">
  <img src="graphics/screenshot_mac.png" />
</p>

## Features



FilesRemote is an SSH file manager that lets you edit files like they are local.

 * Edit files like local:
   * Automatically download and open files in any local editor (configurable).
   * Automatically upload when changes are detected.
   * Especially useful on slow and unstable links, where FUSE+SSHFS would cause too big of a slowdown on the local system.
 * Edit files as root via sudo.
 * Uses SSH auth agent or public key auth when available, with fallback to password based authentication.
 * Cross platform:
   * macOS
   * Windows, [screenshot](graphics/screenshot_win.png)
   * Linux, [screenshot](graphics/screenshot_win.png)

## Installation

To install FilesRemote, follow the instructions below based on your operating system.

### Linux

For Linux users, there are two options available: Debian package and tarball.

**Debian Package:**

1. Download the latest version of the Debian package file (e.g. `filesremote-1.12-Linux-x86_64-static.deb`) from the [release page](https://github.com/allanrbo/filesremote/releases).

2. Open your terminal and navigate to the directory where the downloaded file is located.

3. Run the following command to install FilesRemote (change the version number in actual needs):

   ```shell
   sudo dpkg -i filesremote-1.12-Linux-x86_64-static.deb
   ```

**Tarball:**

1. Download the latest version of the tarball file (e.g. `filesremote-1.12-Linux-x86_64-static.tar.gz`) from the [release page](https://github.com/allanrbo/filesremote/releases).

2. Open your terminal and navigate to the directory where the downloaded file is located.

3. Extract the contents of the tarball using the following command (change the version number in actual needs):

   ```shell
   tar -xzvf filesremote-1.12-Linux-x86_64-static.tar.gz
   ```

4. Change to the extracted directory (change the version number in actual needs):

   ```shell
   cd filesremote-1.12-Linux-x86_64-static
   ```

5. Run the `filesremote` executable to start using FilesRemote.

### macOS

For macOS users, follow the steps below:

1. Download the latest version of the macOS disk image file (e.g. `filesremote-1.12-macOS-x86_64.dmg`) from the [release page](https://github.com/allanrbo/filesremote/releases).

2. Double-click on the downloaded file to open the disk image.

3. Drag and drop the FilesRemote application into the Applications folder.

4. Launch FilesRemote from the Applications folder to start using it.

### Windows

Windows users have two installation options: using the provided ZIP archive or installing via `winget`.

**ZIP Archive:**

1. Download the latest version of the ZIP archive file (e.g. `filesremote-1.12-Windows-x86_64-static.zip`) from the [release page](https://github.com/allanrbo/filesremote/releases).

2. Extract the contents of the ZIP archive to a directory of your choice.

3. Open the extracted folder and run the `filesremote.exe` executable to start using FilesRemote.

**winget:**

Windows users can also install FilesRemote using `winget`, a package manager for Windows. If you have `winget` installed, follow these steps:

1. Open a terminal window.

2. Run the following command to install FilesRemote:

   ```shell
   winget install AllanBoll.FilesRemote
   ```

   This will download and install FilesRemote on your system.

Once you have installed FilesRemote, you can use it to manage and interact with your remote files.

## Usage

Command line usage:
```shell
Usage: filesremote [-h] [-i <str>] [-pw <str>] [[username@]host[:port]]
  -h, --help                    displays help
  -i, --identity-file=<str>     selects a file from which the identity (private key) for public key authentication is read
  -pw, --password=<str>         password to use for authentication and sudo (WARNING: Insecure! Will appear in your shell history!)
Example: filesremote example.com
Example: filesremote 192.168.1.60
Example: filesremote user1@192.168.1.60:2222
Example: filesremote 2001:db8::1
Example: filesremote [2001:db8::1]
Example: filesremote [2001:db8::1]:2222
```

Defaults to your local username and port 22 if unspecified.

### MacOS specific

On first run the app will be blocked, because I do not have an Apple Developer account. From MacOS version 13, it seems that the way to unblock it is to right click and click Open in from Applications:

![Right click and click Open in from Applications](graphics/mac13_security_screenshot.png)

On MacOS versions prior to 13, unblock it in this System Preferences pages:

![Security & Privacy system preferences page](graphics/mac_security_screenshot.png)

After starting the app, go to File -> Preferences and set up the path of your text editor. For example for Sublime Text on MacOS this could be:

    open -a "Sublime Text"

Optionally make aliases for easy command line usage:

    alias filesremote="open -a FilesRemote --args $@"
    alias filesremote_myserver="filesremote user1@192.168.1.60"

## Demo

This demo illustrates the automatic upload feature:

![Demo](graphics/demo.gif)
