# extra-mods/ · 随发布包一起分发的第三方内容 mod

这里的每个文件夹是一个标准 mod 包（`manifest.json` + `content/`），只替换游戏文件，
不含任何可执行代码。`tools/release/package.py` 会把它们原样复制到发布包的
`sdk/bundled-mods/`；安装器（`tools/installer/setup.py: install_bundled_mods`）在首次安装
时把它们放进玩家的 Mods 目录：

| 平台 | 落地位置 |
| --- | --- |
| Windows | `%APPDATA%\WWHD\ModManager\Mods\<id>\` |
| macOS | `~/Library/Application Support/WWHD/ModManager/Mods/<id>/` |
| Linux / SteamOS（AppImage） | `~/.config/wwhd/ModManager/Mods/<id>/` |
| Switch（NRO 不写 SD 卡） | 手动解压到 `sdmc:/switch/wwhd/ModManager/Mods/<id>/` |

两个包的 manifest 都写了 `"enabled_by_default": true`，所以放进 Mods 目录就是开启状态；
Mods 页的开关只是用来关掉它们（一旦关过，选择就记在 `profiles.json` 里）。

## wwhd-feminine-voices

- 作者：BigSharkZ, Niribelle, MobiusWing1
- 内容：美版（`Cafe/US`）语音库里的林克语音，替换 `voice_0.aw` 与 `JaiInit.aaf`
- 原包：Cemu 时代的裸 `content/` 包，本目录里补了 `manifest.json`（`kind: content`）

## linkle-wwhd

- 作者：Linka Mod Creator
- 内容：林可儿（Linkle）模型与语音：`permanent_3d.pack`、`Demo01.szs`、`LinkRM_Room0.szs`，
  以及日版（`Cafe/JP`）语音库，所以美版游戏仍用自己的语音
- 原包：`linkle_in_wwhd_v1.zip`，本目录里补了 `manifest.json`

## 注意

- 这两个包里的文件是从游戏资源改出来的第三方作品，版权归原作者与任天堂所有；随包分发只是为了
  让玩家少一步手动安装，仓库与发布页都不对它们的可用性/合法性作任何担保，介意的话删掉对应
  文件夹即可（`extra-mods/` 不存在时打包机会跳过这一段）。
- `tools/release/guard.py` 对 `sdk/bundled-mods/` 下的 `content/` 树放行（`BUNDLED_MODS`）：
  内容 mod 的本职就是替换游戏文件树，其余检查（游戏代码、密钥、着色器缓存）照旧适用。
