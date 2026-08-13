# linux-pc98 / zedBSD 配置分離・名称変更・userland 再編計画

## 1. 目的

この計画は、zedBSD を汎用のブート用 OS として自立させ、Linux PC-98
配布物に固有の GUI メニュー、Holoris、Remacs などを linux-pc98 側で管理する
ためのリファクタリングを定義する。

最終状態は次のとおりとする。

- zedBSD 単体のディスクイメージは、カーネル起動後に必ず `/bin/sh` を init として
  起動し、テキストシェルへ入る。
- zedBSD 単体イメージには `/etc/zinit.rc` を置かず、GUI メニューを自動起動しない。
- linux-pc98 のイメージだけが、zedBSD の標準 BOOT パーティションへ
  `bootloader/fs/` の製品固有ファイルを重ねてインストールする。
- zedBSD の Noct は主要なユーザランド言語処理系として残す。
- Noct upstream は zedBSD では
  `userland/noct/noct-upstream/`、linux-pc98 では `external/noct/` に置く。
- `external/boots`、`BOOTS_*`、製品名としての `Boots` は互換名を残さず
  `external/zedBSD`、`ZEDBSD_*`、`zedBSD` へ変更する。
- `boot-logo.raw` は未使用なので削除する。
- `.emacs.el` および旧 `EMACS.RC` は配布しない。
- 実装中はコミットもステージも行わず、全変更をレビュー可能な作業ツリーとして残す。

この作業はファイル所有権と配置の整理であり、IDE、CHS、パーティションサイズ、
Linux カーネル、QEMU、BIOS、VM/swap の仕様変更を含まない。

## 2. 作業対象と基準状態

対象リポジトリは次の2つである。

| 役割 | 作業ツリー | 備考 |
|---|---|---|
| 製品イメージ構築 | `/home/awe/linux-pc98` | `external/boots` を `external/zedBSD` へ変更する |
| OS 本体 | `/home/awe/zedBSD` | GitHub の `awemorris/zedBSD` に対応する正本 |

計画作成時点では、同じ zedBSD リポジトリの2つの checkout が一致していない。

- `/home/awe/linux-pc98` が記録している submodule は `3652874`。
- `/home/awe/linux-pc98/external/boots` の実 checkout は `a104858`。
- `/home/awe/zedBSD` は `35e4718` で、swap、イメージ生成、メニュー、テストに
  未コミット変更がある。

実装者は、最初に両方の `git status --short`、`git diff --stat`、`git log -5`、
`git submodule status` を保存すること。`git reset --hard`、`git checkout -- .`、
`git clean`、未確認の stash、未確認の上書きコピーは禁止する。

zedBSD の内容変更は `/home/awe/zedBSD` を正本として行う。ただし、
`external/boots` 側だけにある `a104858` までの変更と、正本側の未コミット変更を
先に比較し、双方を失わない状態へ統合してから配置変更を開始する。統合にも
新規コミットは作らない。競合があれば、リファクタを進める前に差分を列挙して
レビューを求める。

linux-pc98 の submodule は、正本側のレビュー済み内容を指すものとする。
実装中に新しい zedBSD commit を作らないため、最終レビュー時には
submodule の dirty 状態と linux-pc98 側の gitlink 差分を別々に提示する。

## 3. 最終ディレクトリ構造

### 3.1 linux-pc98

```text
linux-pc98/
├── bootloader/
│   ├── README.md
│   ├── build-remacs.sh
│   ├── install-fs.sh
│   ├── verify-fs.sh
│   └── fs/
│       ├── etc/
│       │   └── zinit.rc
│       ├── bin/
│       │   ├── menu.nct
│       │   └── menuback.bmp
│       ├── apps/
│       │   ├── holoris.nct
│       │   └── emacs.nap       # 生成物、gitignore 対象
│       └── home/
│           └── skkjisyo.dic
├── external/
│   ├── zedBSD/                 # awemorris/zedBSD submodule
│   └── noct/                   # awemorris/NoctLang submodule
├── scripts/
│   └── zedbsd-env.sh
└── plan/
    └── zedbsd-bootloader-userland-refactor.md
```

`bootloader/fs/` は BOOT FAT パーティションの絶対パスをそのまま表す。
たとえば `bootloader/fs/apps/holoris.nct` はイメージ内の
`/apps/holoris.nct` へ入る。`bootloader/` 直下はビルド、インストール、検査用で
あり、直下のスクリプト自身を FAT へコピーしてはならない。

`.emacs.el` は作成しない。旧 `EMACS.RC` の内容も移植しない。

### 3.2 zedBSD

```text
zedBSD/
├── userland/
│   ├── crt0.S
│   ├── libc/
│   │   ├── posix.c
│   │   └── syscall.h
│   ├── sh/
│   │   ├── main.c
│   │   ├── applet.c
│   │   └── applet.h
│   ├── bootlinux/
│   │   └── main.c
│   ├── tests/
│   │   └── syscall-smoke.c
│   └── noct/
│       ├── noct-upstream/      # awemorris/NoctLang submodule
│       ├── runtime/            # zedBSD ユーザプロセス用 glue
│       └── integration/        # 共通 NAPI/target adapter
├── apps/
│   ├── bmpview.nct
│   ├── cp.nct
│   ├── hello.nct
│   └── ls.nct
├── src/
│   ├── hal/
│   └── kern/
└── ...
```

最終的にルートの `user/`、`noct/`、`src/noct/` は残さない。
`src/kern/` と `src/hal/` はユーザランド移動のために改変しない。ただし、
ビルド規則からユーザランドへ直接コンパイルされていた `src/kern/env.c` の依存を
外すための最小限の分離は許可する。

## 4. ファイル移動・作成・削除表

### 4.1 zedBSD から linux-pc98 へ移す製品ファイル

すべて内容を比較してから `git mv` 相当で履歴を追えるように移す。
リポジトリをまたぐため、zedBSD 側は削除、linux-pc98 側は追加として見える。

| 現在の zedBSD パス | 最終 linux-pc98 パス | 処理 |
|---|---|---|
| `etc/zinit.rc` | `bootloader/fs/etc/zinit.rc` | 移動。内容は下記の1行に固定 |
| `apps/menu.nct` | `bootloader/fs/bin/menu.nct` | 移動し、絶対パスを最終配置へ修正 |
| `apps/menuback.bmp` | `bootloader/fs/bin/menuback.bmp` | バイナリのまま移動、変換しない |
| `apps/holoris.nct` | `bootloader/fs/apps/holoris.nct` | ソースのまま移動、NAP 化しない |
| `apps/EMACS.RC` | なし | 削除。`.emacs.el` も代替作成しない |
| `apps/boot-logo.raw` | なし | 未使用のため削除 |

`bootloader/fs/etc/zinit.rc` の内容は厳密に次の1行とし、余分な自動起動処理を
追加しない。

```text
/bin/noct /bin/menu.nct
```

`bootloader/fs/bin/menu.nct` 内の製品パスは少なくとも次を満たす。

- 背景: `/bin/menuback.bmp`
- Holoris: `/bin/noct /apps/holoris.nct`、またはシェルの同等な検索規則
- Emacs: `/apps/emacs.nap` を Noct で実行する既存の経路
- Linux: `/bin/linux /vmlinux ...` を直接実行する
- Linux 項目から `/boot.cfg` や `boot.cfg` を source しない

### 4.2 Remacs

| 入力/旧処理 | 最終パス | 処理 |
|---|---|---|
| zedBSD の `scripts/build-remacs-bytecode.sh` | linux-pc98 の `bootloader/build-remacs.sh` | 機能移管後、zedBSD 側を削除 |
| `external/noct/apps/remacs` | 同左 | linux-pc98 側の新 submodule を入力にする |
| 生成 `remacs.nap` | `bootloader/fs/apps/emacs.nap` | 名前を `emacs.nap` に変更し、gitignore |
| `external/noct/apps/remacs/dict/SKK-JISYO.remacs` | `bootloader/fs/home/skkjisyo.dic` | 配布名へコピーし、内容を検証 |
| 旧 `EMACS.RC` | なし | 削除、設定ファイルを配布しない |

`bootloader/build-remacs.sh` は既存スクリプトの検査を維持する。

1. `external/noct` submodule と Remacs の build tool、辞書を検査する。
2. Noct のメッセージ生成など upstream が要求する前処理を実行する。
3. Remacs を既存の最適化済み経路（`-O2` 相当）でコンパイルする。
4. 一時ディレクトリへ `remacs.nap` を生成する。
5. NAP ヘッダと非空サイズを検査する。
6. `bootloader/fs/apps/emacs.nap.part` へコピーし、成功時だけ
   `emacs.nap` へ atomic rename する。
7. 辞書を `bootloader/fs/home/skkjisyo.dic` へ同期し、非空と一致を検査する。

linux-pc98 の `.gitignore` へ次を追加する。

```gitignore
/bootloader/fs/apps/emacs.nap
/bootloader/fs/apps/emacs.nap.part
```

`skkjisyo.dic` は配布構成を明示するファイルなので追跡対象とする。
upstream 辞書更新時は、明示的に同期してレビュー可能な差分として扱う。

### 4.3 zedBSD 内の userland 移動

| 現在 | 最終 | 備考 |
|---|---|---|
| `user/crt0.S` | `userland/crt0.S` | 内容変更なし |
| `user/libc/*` | `userland/libc/*` | include パスだけ更新 |
| `user/sh/*` | `userland/sh/*` | 動作を変えず移動 |
| `user/bootlinux/main.c` | `userland/bootlinux/main.c` | `/bin/linux` のソース |
| `user/tests/*` | `userland/tests/*` | build/test パスを更新 |
| `user/noct/*` | `userland/noct/runtime/*` | ユーザプロセス entry と OS adapter |
| root `noct` submodule | `userland/noct/noct-upstream` | `.gitmodules` も変更 |
| `src/noct/napi.c,h` | `userland/noct/integration/napi.c,h` | ユーザ NAPI 実装へ整理 |
| `src/noct/target.c,h` | `userland/noct/integration/target.c,h` | ユーザ target adapter |
| `src/noct/memory.c,h` | `userland/noct/integration/memory.c,h` | profile 選択。runtime allocator と混同しない |

`user/noct/memory.c,h` と `src/noct/memory.c,h` は役割が異なるので、単純に
上書きしてはならない。前者は `runtime/`、後者は `integration/` に分け、型名と
外部シンボルの重複をビルドで検査する。

現在 `src/noct/noct.c`、`src/noct/platform.c,h`、`src/noct/pc98-beui.c,h`、
`src/noct/noct-m6-script.h` は「Noct をカーネル内で動かしていた時代」のコードを
含む。次の手順で扱う。

1. `rg` と linker map で vmunix の通常起動経路から呼ばれていないことを確認する。
2. 現在これらを使用する host/QEMU テストを一覧化する。
3. 同じ機能を `/bin/noct` と ioctl/syscall 経由で検査するテストへ置換する。
4. 通常 kernel link とテストの双方から参照が消えた後に上記ファイルを削除する。
5. `include/kern/noct.h` も参照がゼロになった時点で削除する。

参照が残っている状態で機械的に削除してテストを弱めてはならない。また、これらを
`src/noct` に残すことで最終構造を曖昧にしてもならない。置換できないテストが
見つかった場合は `userland/noct/tests/support/` へテスト専用 adapter を切り出し、
kernel の通常リンクには戻さない。

現在 `src/kern/env.c` を `/bin/noct` 用 object として再コンパイルしている規則は
廃止する。ユーザランドで必要な環境変数 adapter だけを
`userland/noct/runtime/env.c` として作成し、libc/POSIX API を利用する。
kernel の `src/kern/env.c` 自体の動作は変更しない。

## 5. submodule と名称変更

### 5.1 linux-pc98 の zedBSD submodule

次を一つの論理変更として行う。

1. `external/boots` を `external/zedBSD` へ rename する。
2. `.gitmodules` の section を `external/zedBSD`、path を
   `external/zedBSD`、URL を `https://github.com/awemorris/zedBSD.git` にする。
3. `.git/config` の古い submodule section を `git submodule sync` で同期する。
4. `external/boots` がファイル、symlink、gitdir metadata のいずれにも残って
   いないことを検査する。
5. 互換 symlink や旧変数 alias は作らない。

dirty submodule を rename する前に diff を保存し、rename 後に同じ diff が
残っていることを比較する。

### 5.2 linux-pc98 の Noct submodule

`.gitmodules` に次を追加する。

```ini
[submodule "external/noct"]
    path = external/noct
    url = https://github.com/awemorris/NoctLang.git
```

zedBSD の `userland/noct/noct-upstream` と linux-pc98 の `external/noct` は
独立した gitlink だが、同じ commit を指すことをビルド前に検査する。
片方の working tree をもう片方へ symlink してはならない。

### 5.3 zedBSD の Noct submodule

zedBSD `.gitmodules` は section/path を `userland/noct/noct-upstream` に変更し、
URL は `https://github.com/awemorris/NoctLang.git` のままとする。
旧 root `noct/` の gitdir metadata も正しく移動し、submodule commit はこの
リファクタだけを理由に更新しない。

### 5.4 シンボル、環境変数、文言

linux-pc98 では次を全面変更する。

| 旧 | 新 |
|---|---|
| `scripts/boots-env.sh` | `scripts/zedbsd-env.sh` |
| shell 変数 `boots` | `zedbsd` |
| `BOOTS_RELEASES_DIR` | `ZEDBSD_RELEASES_DIR` |
| `BOOTS_GCC_ROOT` | `ZEDBSD_GCC_ROOT` |
| `BOOTS_MUSL_ROOT` | `ZEDBSD_MUSL_ROOT` |
| `external/boots/...` | `external/zedBSD/...` |
| 製品名としての `Boots` | `zedBSD` |
| `boots-fdd.img` | `zedbsd-fdd.img` |

`configs/boots.cfg` は内容を確認し、zedBSD/Linux BOOT.CFG 用なら
`configs/boot.cfg` へ rename する。すべての参照も同時に更新し、旧名は残さない。

対象は少なくとも次を含む。

- `build.sh`
- `README.md`
- `.gitmodules`
- `scripts/zedbsd-env.sh`
- `scripts/test-image.sh`
- `scripts/make-boot98-debian-image.sh`
- `scripts/build-i386-image.sh`
- `scripts/build-images.sh`
- `scripts/update-kernel.sh`
- `scripts/clean-build.sh`
- `scripts/build-release.sh`
- `scripts/update-boot98-image.sh`
- `scripts/build-bootloader-dist.sh`
- `scripts/mk-pc98-linux-disk.py`

最後に両リポジトリで次を実行し、製品識別子としての旧名をゼロにする。

```sh
rg -n 'external/boots|boots-env|BOOTS_|awemorris/boots|boots-fdd|configs/boots'
rg -n -i '\bBoots\b'
```

英語動詞の “boots” は誤検出として残してよいが、コメントで製品を指すものは
zedBSD に直す。

## 6. zedBSD ビルド規則の編集

### 6.1 `noct.mk`

- `NOCT_ROOT ?= noct` を
  `NOCT_ROOT ?= userland/noct/noct-upstream` へ変更する。
- `-Iuser/...` を `-Iuserland/...` へ変更する。
- zedBSD glue の include path に `userland/noct/runtime` と
  `userland/noct/integration` を明示する。
- object 出力も `build/userland/...` にし、旧 `build/user/...` と
  `build/src/noct/...` を再利用しない。
- host Noct と user Noct の object を混在させない。
- `NOCT_UPSTREAM_COMMIT` は新 submodule path から取得する。

### 6.2 `platform/pc98/platform.mk`

次の object 群を新パスへ置換する。

- `USER_LIBC_OBJS`: `build/userland/crt0.o` と
  `build/userland/libc/*.o`
- `USER_NOCT_GLUE_OBJS`: `build/userland/noct/runtime/*.o` と
  `build/userland/noct/integration/*.o`
- `USER_SH_OBJS`: `build/userland/sh/*.o`
- `USER_BOOTLINUX_OBJS`: `build/userland/bootlinux/main.o`
- `INIT.ELF` test object: `build/userland/tests/syscall-smoke.o`

`$(BUILD)/user/noct/napi.o: src/noct/napi.c` のような旧パスを入力とする特別規則は
削除し、新しい source/object の通常の1対1規則にする。
`src/kern/env.c` を user ELF へ入れる特別規則も削除する。

出力バイナリ名は互換性のためではなく現在の ABI として維持する。

- `build/bin/sh` → イメージの `/bin/sh`
- `build/bin/noct` → `/bin/noct`
- `build/bin/linux` → `/bin/linux`
- `build/INIT.ELF` は syscall smoke test 用のまま

### 6.3 top-level `Makefile` と include

- ディレクトリ説明を `userland/` と新 Noct 配置へ更新する。
- host tests の source dependency を新パスへ変更する。
- `#include "user/..."`、`#include "noct/..."` がソースツリーの偶然の
  `-I` 順序に依存しないよう、公開 UAPI、runtime private header、upstream header を
  区別する。
- generated dependency `.d` を一度除去して clean build し、旧パスの依存が
  キャッシュに残らないことを確認する。

### 6.4 shell の Emacs パス

`userland/sh/main.c` へ移動した後、旧 `/apps/remacs.nap` 参照を
`/apps/emacs.nap` へ変更する。シェルの一般的な `.nct`/`.nap` 検索規則は維持し、
Holoris 専用の built-in は追加しない。

`/etc/zinit.rc` の「存在すれば1秒待って source、なければ対話シェル」という
現在の shell 動作は維持する。標準 zedBSD イメージには同ファイルがないため即座に
対話シェルとなり、linux-pc98 overlay では GUI メニューが起動する。

## 7. zedBSD 標準イメージを bare にする

### 7.1 `scripts/install-image.sh`

このスクリプトは zedBSD 標準ファイルだけを BOOT FAT へ入れる責務に縮小する。

削除する処理と変数:

- `ZEDBSD_ZINIT_RC`、`ZEDBSD_ZINIT_DISABLE`
- `ZEDBSD_MENU`、`ZEDBSD_MENU_BACKGROUND`
- zinit、menu、menuback のインストール
- Holoris のコンパイルとインストール
- Remacs のコンパイル、NAP、辞書のインストール
- `boot-logo.raw` のインストール

維持する処理:

- IPL/PBR、`IO.SYS`、`vmunix`
- swapfile
- `/bin/sh`、`/bin/noct`、`/bin/linux`
- zedBSD に残す汎用 Noct scripts: `hello.nct`、`ls.nct`、`cp.nct`、
  `bmpview.nct` または既存のコンパイル済み bmpview
- kernel image、BOOT.CFG、既存の明示的な追加ファイル hook
- FAT16 のディレクトリ生成と検証

インストール完了後に次のファイルがないことを標準イメージテストで検査する。

```text
/etc/zinit.rc
/bin/menu.nct
/bin/menuback.bmp
/apps/holoris.nct
/apps/holoris.nap
/apps/emacs.nap
/home/skkjisyo.dic
```

### 7.2 `scripts/make-hdd-image.sh` / `scripts/make-fdd-image.sh`

- zinit/menu/Remacs/Holoris を暗黙に渡す処理を削除する。
- HDD は既存の FAT16、H=8、swap、パーティションサイズを変更しない。
- FDD も GUI 製品イメージではなく bare zedBSD とする。
- 起動ファイル名 `vmunix` と `/bin/sh` init の仕様を変えない。

### 7.3 zedBSD 側から削除する製品 scripts/tests

linux-pc98 側へ機能を移し、同等テストが動いてから削除する。

- `scripts/build-remacs-bytecode.sh`
- `scripts/build-holoris-bytecode.sh`
- `scripts/test-autoexec-remacs.sh`
- `scripts/test-remacs.sh`
- `scripts/test-beui-holoris.sh`
- 製品 zinit/menu を前提とする `scripts/test-hdd-boot.sh`
- 製品 zinit/menu を前提とする `scripts/test-linux-handoff.sh` の部分
- `autoexec-remacs-qemu-test` など対応する Make target

`test-beui-menu.sh` のように、テスト用 menu をその場で生成し BeUI API 自体を
検査するものは zedBSD に残す。`test-boot-cancel.sh` と
`test-console-scroll.sh` は shell/zinit API の汎用テストなので、任意の一時
`zinit.rc` を明示注入する形に整理して残してよい。標準イメージが zinit を含むと
仮定してはならない。

## 8. linux-pc98 bootloader overlay

### 8.1 `bootloader/install-fs.sh`

新規作成する。責務は、zedBSD が作成済みの BOOT FAT パーティションへ
`bootloader/fs/` を重ねることだけである。

必要な引数:

```text
install-fs.sh IMAGE BOOT_PARTITION_OFFSET
```

既存スクリプトが sector 単位を使う場合は `--offset-sectors` を用意し、byte offset
と混同しない。内部では zedBSD installer と同じ mtools の
`IMAGE@@BYTE_OFFSET` 形式に一度だけ正規化する。

実装手順:

1. image が通常ファイルであり、offset が image 内にあることを検査する。
2. `mdir` で対象が FAT かつ zedBSD BOOT パーティションであることを検査する。
3. `bootloader/build-remacs.sh` を呼び、`emacs.nap` を準備する。
4. manifest にある全 source が非空であることを先に検査する。
5. `ETC`、`BIN`、`APPS`、`HOME` を `mmd` で作る。既存なら許容する。
6. manifest を明示的に `mcopy -o` する。再帰コピーや glob は使わない。
7. `verify-fs.sh` を呼ぶ。

manifest は次の6ファイルに限定する。

| source | FAT destination |
|---|---|
| `fs/etc/zinit.rc` | `::ETC/ZINIT.RC` |
| `fs/bin/menu.nct` | `::BIN/MENU.NCT` |
| `fs/bin/menuback.bmp` | `::BIN/MENUBACK.BMP` |
| `fs/apps/holoris.nct` | `::APPS/HOLORIS.NCT` |
| `fs/apps/emacs.nap` | `::APPS/EMACS.NAP` |
| `fs/home/skkjisyo.dic` | `::HOME/SKKJISYO.DIC` |

表は6ファイルであり、`.emacs.el` は含めない。実装者は manifest 件数を
ハードコードする場合も6とする。

FAT16 の LFN、case policy、SFN encoder は変更しない。ホスト側は小文字名、
FAT 上の物理名は mtools が作る 8.3 大文字名、zedBSD VFS からは既存方針どおり
小文字で見える。

このスクリプトが行ってはいけないこと:

- FAT の format
- partition table、CHS、IPL、PBR の書換え
- `vmunix`、swapfile、Linux root/swap partition の変更
- Linux kernel の置換
- `boot.cfg` の自動生成

### 8.2 `bootloader/verify-fs.sh`

新規作成し、少なくとも次を検査する。

- manifest 6ファイルが FAT 上に存在し、非空
- `zinit.rc` が `/bin/noct /bin/menu.nct` の1行
- BMP header が `BM`、640x480、8bpp
- `emacs.nap` が Noct bytecode header を持つ
- menu 内の Linux action が `/bin/linux` を指し、`boot.cfg` を source しない
- menu 内の Holoris/background/Emacs path が manifest と一致
- FAT の `vmunix`、swapfile、`/bin/sh`、`/bin/noct`、`/bin/linux` が overlay
  前後で同じ hash/size

### 8.3 image builder への組込み

zedBSD `install-image.sh` が標準 BOOT ファイルを入れた直後、かつ image の最終
検査・公開前に `bootloader/install-fs.sh` を呼ぶ。

少なくとも次の linux-pc98 経路へ組み込む。

- busybox image: `scripts/build-i386-image.sh`
- Debian 13 image: `scripts/make-boot98-debian-image.sh`
- batch build: `scripts/build-images.sh`
- kernel/update path: `scripts/update-kernel.sh`、`scripts/update-boot98-image.sh`
- test image: `scripts/test-image.sh`
- distribution archive: `scripts/build-bootloader-dist.sh`

共通 helper を作って重複を避ける。各経路は BOOT partition の byte offset を
既存 partition metadata から渡し、固定値を複製しない。

標準 zedBSD image を要求する経路では overlay を呼ばない。linux-pc98 の製品
image ではデフォルトで overlay を呼ぶ。テスト専用に無効化が必要なら、名前は
`LINUX_PC98_BOOTLOADER_OVERLAY=0` のように製品側のものとし、`BOOTS_*` 互換名は
作らない。

## 9. linux-pc98 `build.sh` と配布処理

`build.sh` の既存コマンド体系を維持しつつ所有権を更新する。

- zedBSD 本体 build/test/install は `external/zedBSD` を呼ぶ。
- `remacs` は `bootloader/build-remacs.sh` を呼ぶ。
- `remacs-test` は linux-pc98 側へ移した製品 QEMU test を呼ぶ。
- bootloader image/dist は zedBSD 標準成果物を作ってから overlay を適用する。
- help 文言は “Boots binaries” ではなく “zedBSD binaries” または
  “linux-pc98 bootloader image” と役割を区別する。
- 出力 `boots-fdd.img` は `zedbsd-fdd.img` へ変更する。

`scripts/build-bootloader-dist.sh` は旧 `CMD/REMACS.NAP`、`AUTOEXEC.NCT`、
`EMACS.RC`、`BOOTS.CFG` を個別に組み立てる処理を削除し、標準 zedBSD 成果物と
`bootloader/fs` manifest を唯一の入力にする。配布 ZIP 内でも最終 FAT 配置と
同じ名前を使い、古い `REMACS.NAP` や `.remacs.el` を残さない。

## 10. テスト計画と段階ゲート

各ゲートを通過してから次へ進む。失敗時はその段階の移動だけを戻し、後段の
アドホック修正で隠さない。

### Gate 0: 差分保護

- 両リポジトリと全 submodule の status/log/diff を保存した。
- `a104858` の Linux menu fix と standalone zedBSD の swap/image 変更が失われて
  いない。
- commit、stage、reset、clean を行っていない。

### Gate 1: submodule rename

- linux-pc98 の `external/zedBSD` が同じ checkout を指す。
- zedBSD の upstream Noct が `userland/noct/noct-upstream` で初期化できる。
- linux-pc98 の `external/noct` と zedBSD upstream Noct の commit が一致する。
- 旧 path/URL/環境変数の検索がゼロ。

### Gate 2: zedBSD userland clean build

- 完全 clean build が成功する。
- `vmunix`、`/bin/sh`、`/bin/noct`、`/bin/linux`、`INIT.ELF` が生成される。
- `readelf`/既存 `check-user-elf.py` が全 user ELF を受理する。
- kernel map に `src/noct` 由来の旧 in-kernel interpreter が残らない。
- user ELF が `src/kern/env.o` や HAL/kernel private symbol を参照しない。

### Gate 3: bare zedBSD image

- QEMU の互換 BIOS POST は5秒未満。
- VFS が BOOT FAT を mount する。
- init `/bin/sh` が起動し、zinit 待ちをせず prompt を表示する。
- `ls /bin` で `sh`、`noct`、`linux` が見える。
- 汎用 `ls.nct`、`cp.nct`、`hello.nct`、`bmpview.nct` が利用できる。
- 製品 manifest の6ファイルが存在しない。
- Linux handoff、swap、複数 partition mount の既存テストに退行がない。

### Gate 4: linux-pc98 overlay unit test

- 一時 image に overlay を1回適用して manifest が一致する。
- 同じ overlay を2回適用して成功し、内容が変化しない（idempotent）。
- overlay 前後で BOOT system files と swapfile の hash/size が同じ。
- 破損 image、範囲外 offset、未生成 emacs.nap で明確に失敗する。

### Gate 5: linux-pc98 GUI product test

- `zinit.rc` が `/bin/noct /bin/menu.nct` を起動する。
- 640x480/8bpp の背景と menu が表示される。
- key input で変更された menu item だけが再描画される既存挙動を維持する。
- Holoris が `/apps/holoris.nct` から起動する。
- Emacs が `/apps/emacs.nap` から起動し、キー入力に応答する。
- `/home/skkjisyo.dic` が読める。
- Linux menu item が `/bin/linux` を呼び、Linux kernel が起動する。
- menu から shell へ戻る経路も動作する。

### Gate 6: 実 image 回帰

busybox と Debian 13 の両方で次を確認する。

- image サイズ、128MB BOOT FAT、64MB swapfile、Linux partition サイズが作業前と
  同じ方針
- IDE H=8 image geometry を維持
- QEMU の最新 `/home/awe/qemu-pc98/build/qemu-system-i386` を使用
- GUI menu、Emacs、Holoris、shell、Linux handoff が成功
- QEMU 用の診断で5秒以上 POST に留まった場合は失敗扱い

実機確認項目として、Cirrus 8bpp、GDC fallback、console 復帰時 TVRAM、IDE access
をチェックリストへ残す。ただしこのリファクタで display/IDE driver を変更しない。

## 11. 変更してはいけないもの

この計画の実装では次を変更しない。

- `/home/awe/qemu-pc98` の source/binary/BIOS
- `/home/awe/linux-pc98/external/linux` の Linux source
- IDE driver、partition scanner、FAT implementation、inode/VFS semantics
- CHS policy（image H=8、BIOS の小容量 H=4 判定を含む）
- BOOT FAT 128MB、swapfile 64MB、busybox/Debian image sizing
- VM commit、brk/mmap、swap allocator
- syscall ABI、INT 0xc2、ELF loader、scheduler
- framebuffer/console/BeUI API と 8bpp/24bpp mode logic
- Noct upstream source。必要な修正は別リポジトリの別レビューとする
- generic zedBSD apps `ls.nct`、`cp.nct`、`hello.nct`、`bmpview.nct` の機能
- zedBSD init が `/bin/sh` を起動する kernel policy

また、互換性目的で次を作らない。

- `external/boots` symlink
- `scripts/boots-env.sh` wrapper
- `BOOTS_*` alias
- `/apps/remacs.nap` の複製
- `AUTOEXEC.NCT`
- `.emacs.el`、`.emacs`、`.remacs.el`
- `holoris.nap`

## 12. レビュー提出物

実装完了時はコミットせず、次をレビューへ渡す。

1. `/home/awe/linux-pc98` の `git status --short`、`git diff --stat`、`git diff`。
2. `/home/awe/zedBSD` の同じ3点。
3. 全 submodule の path、URL、commit 一覧。
4. old-name `rg` の結果（英語動詞以外ゼロ）。
5. bare zedBSD image の file manifest と起動ログ。
6. busybox/Debian 製品 image の overlay manifest と起動ログ。
7. Remacs build log、NAP header 検査、辞書一致検査。
8. GUI menu、Holoris、Emacs、Linux handoff の QEMU テスト結果。
9. 実行しなかった実機テストがあれば、その項目と理由。

削除・移動は、対応する新配置とテストが成立した後にだけ行う。最終レビューでは
rename detection が働くよう `git diff --summary` も添える。

## 13. 実装順序の要約

1. 既存の2つの zedBSD checkout と dirty diff を保護・統合する。
2. submodule 名、URL、環境変数を zedBSD へ変更する。
3. zedBSD の `user/`、root `noct/`、`src/noct/` を `userland/` へ再編する。
4. clean build と user ELF 検査を通す。
5. zedBSD installer から製品 GUI/Remacs/Holoris を外し、bare image を検証する。
6. linux-pc98 に `bootloader/fs/` と `external/noct` を作る。
7. zinit、menu、background、Holoris を移し、`boot-logo.raw` と `EMACS.RC` を削除する。
8. Remacs build と overlay install/verify scripts を作る。
9. linux-pc98 の全 image builder に overlay を接続する。
10. 製品 tests を linux-pc98 へ移し、bare/product の回帰を別々に実行する。
11. 旧名、旧 path、不要ファイルがゼロであることを確認する。
12. commit/stage せず、両リポジトリの差分とテスト結果をレビューへ提出する。
