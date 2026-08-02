# Debianネイティブ互換性検証記録（2026-08-02）

## 結論

対象サーバーでは、現時点で本MODを有効化しない。

Windows、Wine/Proton、VM、セーブデータ移行は採用しない。既存セーブを維持したままDebianネイティブで進める方針は維持するが、現在確認できたLinux UE4SS基盤は、本MODの4つのUFunctionフックを安全に提供できない。

本検証後の運用状態は次のとおり。

- Palworldサービス: 稼働中
- 安定版 `libUE4SS.so`: 使用中
- `PalworldServerAutoRevive`: `mods.txt` で無効（`0`）
- セーブデータ: 復元操作なし、イベント受入試験なし

## 対象環境

- Debian GNU/Linux 13.6 trixie
- x86_64
- Palworld Dedicated Server ネイティブLinux版
- Palworld本体: v1.0.2.100993
- Steam AppID: 2394010
- 初期確認時のSteam buildid: 24445026
- 導入先: `/home/serveradmin/servers/palworld`

## バックアップ

MOD基盤を変更する前に、セーブとLinuxServer設定をバックアップした。

導入前バックアップ:

- `/home/serveradmin/servers/palworld-backups/pre-mod-palworld-20260802T163237JST.tar.gz`
- SHA256: `3919104f73d16ff554e062272f91d63281c11f660dcffb8877e07d2ed1543ee5`

カスタムUE4SS試験直前の追加バックアップ:

- `/home/serveradmin/servers/palworld-backups/pre-custom-linux-ue4ss-20260802T180155JST.tar.gz`
- SHA256: `4b151fc3398bd592093e2424a241bec6e571b95313a5a6918fedc4626a9ce5e3`

## 確認した基盤

### 安定版UE4SS Linux v3.0.2相当

安定版ライブラリはPalworldサービスを起動できる。しかし対象実行ファイルではLinux limited modeとなり、Lua MODが必要とするUEのUFunction/ProcessEventフックを利用できない。

安定版のSHA256:

```text
057d627537ab5235e1a26cbc16ca110fdd320a397484fbcd0dff30ee2cb09281
```

### Linux nativeポートの新しいビルド

Linux nativeポートのソースを対象サーバー上のユーザー領域でビルドし、RustやGUIなど不要な機能を無効化して検証した。システム全体へのパッケージ導入は行っていない。

Palworld用 `MemberVariableLayout.ini` を使うと、次の解決までは確認できた。

- `GUObjectArray`
- `FName` constructor
- `ProcessEvent`
- Linux full mode判定

ただし、UE4SS初期化中にSIGSEGVが発生し、systemdがPalworldプロセスを再起動した。LoadMapフックを無効化した試験、およびMODのフック登録をゲームスレッドへ渡す試験でも、安定稼働には至らなかった。

カスタム試験ライブラリのSHA256:

```text
c9593e1af5e46aa1f4a22154b643caa90db753cb016d07367a856c78be689679
```

このライブラリは運用ファイルから除去し、安定版へ戻した。

## 本MODの試験判定

| 項目 | 判定 |
| --- | --- |
| 事前バックアップ | PASS |
| Debianネイティブ維持 | PASS |
| 既存セーブの維持 | PASS |
| UE4SS安定版のサービス起動 | PASS |
| 本MODのLua読込 | 未採用基盤で一部確認 |
| 4フックの安定登録 | FAIL |
| パルボックス移動イベント | 未実施 |
| リスポーン完了イベント | 未実施 |
| 自ギルド拠点進入イベント | 未実施 |
| 本番有効化 | NO-GO |

イベント受入試験を行っていないため、MODの機能を「動作確認済み」とは扱わない。

## 再開条件

次の条件をすべて満たすLinux UE4SS基盤が得られた場合だけ、再検証する。

1. PalworldプロセスをSIGSEGVさせず、サービスを安定起動できる。
2. `ProcessEvent`フックが利用可能である。
3. 本MODの次の4登録がログ上で成功する。
   - `RequestMoveToPalBox_ToServer_Rep`
   - `OnUpdateSlot`
   - `CallRespawnDelegate`
   - `OnEnterBaseCamp`
4. サービス再起動後も同じ状態を再現できる。
5. バックアップ確認後に、仕様書の受入試験を完了できる。

条件を満たすまで、`mods.txt` の設定は次のままにする。

```text
PalworldServerAutoRevive : 0
```

## 方針

データ互換性と性能を優先し、実行時にセーブを変換する方式や、別OS上でMODを動かす方式は採用しない。次の候補は、互換性のあるLinux UE4SSの入手・更新、またはPalworld本体の更新に合わせたネイティブLinuxフック層の再設計である。いずれも、対象サービスを停止した検証環境とバックアップを用いて再評価する。
