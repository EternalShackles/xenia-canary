# Netplay Fork

This is a fork of [Xenia Canary](https://github.com/xenia-canary/xenia-canary) which implements online multiplayer features. The REST API powering this fork can be found [here](https://github.com/AdrianCassar/Xenia-WebServices#xenia-web-services).

Current online sessions are displayed on [https://xenia-netplay-2a0298c0e3f4.herokuapp.com/](https://xenia-netplay-2a0298c0e3f4.herokuapp.com/).

---

## [Netplay Compatibility](SupportedTitles.md#supported-games)

You can find a list of working games **[here](SupportedTitles.md#supported-games)**.

The netplay compatibility list contains games that are working in some form of netplay/xbox live/systemlink. These games may not be fully functional.

A game is added when two or more players can connect to each other and enter in-game together.

---

## Config Setup

You can watch a video guide at [https://www.youtube.com/watch?v=NnjGLTQig3U](https://www.youtube.com/watch?v=NnjGLTQig3U).

To connect to a **Xenia Web Server** you can either [privately host](https://github.com/AdrianCassar/Xenia-WebServices#xenia-web-services) it yourself locally or connect to my public server.

The `api_address` must be changed from its default value to a online or locally hosted web server address.\
A placeholder address is used because pointing to a public server may change or become offline.
```toml
api_address = "https://xenia-netplay-2a0298c0e3f4.herokuapp.com"
```

UPnP is disabled by default for security reasons, you must enable it to host sessions.
```toml
upnp = true
```

Default gamertags are generated you can change your gamertag like this.
```toml
user_0_name = "Gamertag Here"
```

---

## [Netplay Mousehook](https://github.com/marinesciencedude/xenia-canary-mousehook/tree/netplay_canary_experimental#mousehook)
- [Releases](https://github.com/marinesciencedude/xenia-canary-mousehook/releases?q=Netplay)

Netplay mousehook is a fork of netplay which adds support for playing a few games with mouse and keyboard.

---

## FAQ

Where can I download the Xenia Canary Netplay build?

- You can download the very latest build from [GitHub Actions](https://github.com/AdrianCassar/xenia-canary/actions?query=is%3Asuccess+event%3Apush+actor%3AAdrianCassar+branch%3Anetplay_canary_experimental) or stable builds from [releases](https://github.com/AdrianCassar/xenia-canary/releases).

Is UPnP Supported?
- Yes, UPnP is supported it must be enabled in the config to host sessions.

Can I host Xenia Web Services?

- Yes, [Xenia Web Services](https://github.com/AdrianCassar/Xenia-WebServices#xenia-web-services).

Are games dependent on servers?

- Yes a lot of games are dependent on servers therefore will not work, unless a server is developed for that game.

Can I use multiple PCs on the same network?

- Yes this will require hosting [Xenia Web Services](https://github.com/AdrianCassar/Xenia-WebServices#xenia-web-services) on your local network. However, connecting to the API via the internet on the same network will require a VPN which may cause issues connecting with some games.

Is XLink Kai supported?

- XLink Kai is not supported.

Is there a netplay mousehook build?

- Yes, download it from [Netplay Mousehook](https://github.com/marinesciencedude/xenia-canary-mousehook/releases?q=Netplay).

---

## Linux Notes

<details>
  <summary>Failure to Bind to Ports</summary>

### Failure to Bind to Ports
Binding to ports <= 1024 will usually fail on Linux as they are protected by default. To verify this is an issue you are encountering, search your log for the following message:
`NetDll_WSAGetLastError: 10013`

To fix this run this command:
```console
sudo sysctl net.ipv4.ip_unprivileged_port_start=999
echo 'sysctl net.ipv4.ip_unprivileged_port_start=999' | sudo tee /etc/sysctl.d/99-xenia.conf
```
This command configures privileged ports to start at port 999 instead of port 1024 in this logon session and future logons. This should allow for most games to now bind. 

If you are still seeing `NetDll_WSAGetLastError: 10013` in logs after running this, you can try rerunning the previous commands with a number lower than `999`. `23` should solve every case. You can try `0` but it will prevent you from running ssh.

It should also be noted that due to the way Steam Decks handle configuration, you will need to rerun this command on every reboot.

</details>

---

<p align="center">
    <a href="https://github.com/xenia-canary/xenia-canary/tree/canary_experimental/assets/icon">
        <img height="120px" src="https://raw.githubusercontent.com/xenia-canary/xenia/master/assets/icon/128.png" />
    </a>
</p>

<h1 align="center">Xenia Canary - Xbox 360 Emulator</h1>

Xenia is an experimental emulator for the Xbox 360. For more information, see the
[Xenia wiki](https://github.com/xenia-canary/xenia-canary/wiki).

Come chat with us about **emulator-related topics** on [Discord](https://discord.gg/Q9mxZf9).
For developer chat join `#dev` but stay on topic. Lurking is not only fine, but encouraged!
Please check the [FAQ](https://github.com/xenia-project/xenia/wiki/FAQ) page before asking questions.
We've got jobs/lives/etc, so don't expect instant answers.

Discussing illegal activities will get you banned.

## Status

Buildbot | Status | Releases
-------- | ------ | --------
Windows | [![CI](https://github.com/xenia-canary/xenia-canary/actions/workflows/Windows_build.yml/badge.svg?branch=canary_experimental)](https://github.com/xenia-canary/xenia-canary/actions/workflows/Windows_build.yml) [![Codacy Badge](https://app.codacy.com/project/badge/Grade/cd506034fd8148309a45034925648499)](https://app.codacy.com/gh/xenia-canary/xenia-canary/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade) | [Latest](https://github.com/xenia-canary/xenia-canary/releases/latest) ◦ [All](https://github.com/xenia-canary/xenia-canary/releases)
Linux | Curently unsupported
Netplay Build | | [Latest](https://github.com/AdrianCassar/xenia-canary/releases/latest)

## Quickstart

See the [Quickstart](https://github.com/xenia-project/xenia/wiki/Quickstart) page.

## FAQ

See the [frequently asked questions](https://github.com/xenia-project/xenia/wiki/FAQ) page.

## Game Compatibility

See the [Game compatibility list](https://github.com/xenia-project/game-compatibility/issues)
for currently tracked games, and feel free to contribute your own updates,
screenshots, and information there following the [existing conventions](https://github.com/xenia-project/game-compatibility/blob/master/README.md).

## Building

See [building.md](docs/building.md) for setup and information about the
`xb` script. When writing code, check the [style guide](docs/style_guide.md)
and be sure to run clang-format!

## Contributors Wanted!

Have some spare time, know advanced C++, and want to write an emulator?
Contribute! There's a ton of work that needs to be done, a lot of which
is wide open greenfield fun.

**For general rules and guidelines please see [CONTRIBUTING.md](.github/CONTRIBUTING.md).**

Fixes and optimizations are always welcome (please!), but in addition to
that there are some major work areas still untouched:

* Help work through [missing functionality/bugs in games](https://github.com/xenia-project/xenia/labels/compat)
* Reduce the size of Xenia's [huge log files](https://github.com/xenia-project/xenia/issues/1526)
* Skilled with Linux? A strong contributor is needed to [help with porting](https://github.com/xenia-project/xenia/labels/platform-linux)

See more projects [good for contributors](https://github.com/xenia-project/xenia/labels/good%20first%20issue). It's a good idea to ask on Discord and check the issues page before beginning work on
something.

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).
