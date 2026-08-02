# Debian導入手順

この手順は、Palworldサービスを停止した状態で実施する。サービスが起動中なら、最初に正常停止してから続行する。

## 1. 変数と事前条件

```bash
set -eu

GAME_ROOT=/home/serveradmin/servers/palworld
BIN="$GAME_ROOT/Pal/Binaries/Linux"
MOD_DIR="$BIN/Mods/PalworldServerAutoRevive"
UE4SS_VERSION=v3.0.2
UE4SS_URL="https://github.com/XarminaEu/ue4ss-linux/releases/download/$UE4SS_VERSION/ue4ss-linux-v3.0.2.tar.gz"
UE4SS_SHA256=bbc85d0d0288afa5475f9eab871f28066b758f24e9a9e45472764cb3abc1df02
ARCHIVE="$GAME_ROOT/.staging/ue4ss-linux-v3.0.2/ue4ss-linux-v3.0.2.tar.gz"

if systemctl --user is-active --quiet palworld.service; then
  echo "palworld.service must be stopped before installation" >&2
  exit 1
fi

mkdir -p "$GAME_ROOT/.staging/ue4ss-linux-v3.0.2"
```

## 2. セーブと設定をバックアップ

バックアップはMODファイルを置く前に作成する。

```bash
BACKUP_DIR=/home/serveradmin/servers/palworld-backups
STAMP=$(date +%Y%m%dT%H%M%S%z)
mkdir -p "$BACKUP_DIR"

tar -czf "$BACKUP_DIR/pre-mod-palworld-$STAMP.tar.gz"   -C "$GAME_ROOT"   Pal/Saved/SaveGames   Pal/Saved/Config/LinuxServer

sha256sum "$BACKUP_DIR/pre-mod-palworld-$STAMP.tar.gz"
tar -tzf "$BACKUP_DIR/pre-mod-palworld-$STAMP.tar.gz"   | grep -E 'Pal/Saved/(SaveGames|Config/LinuxServer)'
```

## 3. UE4SS Linux基盤を取得して検証

対象アーカイブのSHA256は、リリース公開値と完全一致させる。

```bash
if [ ! -f "$ARCHIVE" ]; then
  curl -fL --retry 3 -o "$ARCHIVE" "$UE4SS_URL"
fi

test "$(sha256sum "$ARCHIVE" | awk '{print $1}')" = "$UE4SS_SHA256"

UE4SS_EXTRACT="$GAME_ROOT/.staging/ue4ss-linux-v3.0.2/extracted"
rm -rf "$UE4SS_EXTRACT"
mkdir -p "$UE4SS_EXTRACT"
tar -xzf "$ARCHIVE" -C "$UE4SS_EXTRACT"
test -f "$UE4SS_EXTRACT/libUE4SS.so"
```

本サーバーのPalServer実行ファイルはstrip済みのため、初回起動後にUE4SSが関数解決できているかログで確認する。解決できない場合は、MODを有効化したまま運用せず、`UE4SS_Addresses.ini` の対応を先に行う。

```bash
test ! -e "$BIN/libUE4SS.so" || {
  echo "libUE4SS.so already exists; inspect it before replacing" >&2
  exit 1
}
install -m 0755 "$UE4SS_EXTRACT/libUE4SS.so" "$BIN/libUE4SS.so"
```

## 4. UE4SS設定とMODをClone

```bash
mkdir -p "$BIN/Mods"

if [ ! -e "$BIN/UE4SS-settings.ini" ]; then
  cat > "$BIN/UE4SS-settings.ini" <<'EOF'
[General]
EnableHotReloadSystem=false
EnableAutoReloadingLuaMods=false
UseCache=false
bUseUObjectArrayCache=false
InvalidateCacheIfDLLDiffers=true
EnableDebugKeyBindings=false

[EngineVersionOverride]
MajorVersion=5
MinorVersion=1
DebugBuild=false
EOF
fi

if [ ! -e "$MOD_DIR/.git" ]; then
  git clone https://github.com/p7672716/palworld-server-auto-revive.git "$MOD_DIR"
else
  git -C "$MOD_DIR" pull --ff-only
fi

touch "$BIN/Mods/mods.txt"
grep -Fqx 'PalworldServerAutoRevive : 1' "$BIN/Mods/mods.txt"   || printf '%s\n' 'PalworldServerAutoRevive : 1' >> "$BIN/Mods/mods.txt"
```

Linuxでは大文字小文字を区別するため、`scripts/main.lua` のパスを変更しない。

## 5. systemdへLD_PRELOADを永続設定

既存のサービスユニット本文を上書きせず、ユーザーサービスのdrop-inを使う。

```bash
DROPIN_DIR="$HOME/.config/systemd/user/palworld.service.d"
mkdir -p "$DROPIN_DIR"

cat > "$DROPIN_DIR/ue4ss.conf" <<EOF
[Service]
Environment=LD_PRELOAD=$BIN/libUE4SS.so
EOF

systemctl --user daemon-reload
systemctl --user show palworld.service -p Environment
```

## 6. 起動と基盤確認

最初の起動では、ゲームプレイ試験より先にUE4SSロードを確認する。

### 対象Debianで確認済みの制約

対象サーバー（Palworld `v1.0.2.100993`）では、安定版UE4SS Linux v3.0.2はロードできますが、Linux limited modeとなりUEフックが利用できません。最新確認版 v3.0.26-linux-devは、Palworld用 `MemberVariableLayout.ini` を配置してfull modeまで進むものの、MODを無効にした状態でもUE4SS初期化中にSIGSEGVとなりました。

そのため、対象サーバーではCloneと基盤配置まで行ったうえで、現在は次の行を無効化しています。

```text
PalworldServerAutoRevive : 0
```

互換性のあるUE4SS Linuxビルドが確認できるまで、`: 1`へ変更してイベント試験へ進めないこと。互換ビルドが得られた後に、バックアップ、フック登録4件、ゲーム内受入試験の順で再開する。


```bash
systemctl --user start palworld.service
sleep 10
systemctl --user status palworld.service --no-pager
journalctl --user -u palworld.service -n 120 --no-pager
```

次のログが必要:

- `libUE4SS.so` がロードされた記録
- `[PalworldServerAutoRevive] main.lua loaded`
- 4つのフック登録成功行
- クラッシュ、missing address、Lua load errorがないこと

ログ確認が失敗した場合はサービスを停止し、フックイベント試験へ進まない。

## 7. ロールバック

```bash
systemctl --user stop palworld.service || true

# MODを無効化してから退避する
sed -i 's/^PalworldServerAutoRevive : 1$/PalworldServerAutoRevive : 0/'   "$BIN/Mods/mods.txt"
mv "$MOD_DIR" "$MOD_DIR.disabled.$(date +%Y%m%dT%H%M%S)"

rm -f "$HOME/.config/systemd/user/palworld.service.d/ue4ss.conf"
systemctl --user daemon-reload
rm -f "$BIN/libUE4SS.so"

# このファイルを本手順で新規作成した場合だけ退避または削除する
# rm -f "$BIN/UE4SS-settings.ini"

systemctl --user start palworld.service
```

セーブを戻す必要がある場合は、サービス停止中に導入前バックアップから
`Pal/Saved/SaveGames` と `Pal/Saved/Config/LinuxServer` を復元する。復元前に現在の同じ2ディレクトリを別名で退避する。
