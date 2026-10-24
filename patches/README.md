# patches

对**上游依赖**打的补丁。构建时由 CI 克隆上游、应用这里的补丁（见 `.github/workflows/build.yml`
的「Clone upstream sing-box and apply our patch」步骤）。

## sing-box-0001-platform-log-disable-colors.patch

- 目标：`SagerNet/sing-box`，版本由 `build.yml` 的 `SING_BOX_VERSION` 钉住（当前 v1.14.4）
- 内容：平台日志（journald / 系统日志）用的 formatter 关掉 ANSI 颜色。
  上游 `log/observable.go` 里那句

  ```go
  /*if platformWriter != nil {
      factory.platformFormatter.DisableColors = platformWriter.DisableColors()
  }*/
  ```

  是被注释掉的，所以平台日志会带上颜色转义符；补丁在 `NewDefaultFactory` 的
  `platformFormatter` literal 里直接置 `DisableColors: true`。
- 为什么是补丁而不是 fork：这个改动总共 1 行。为此维护一个完整的 sing-box fork
  （10MB、每次升上游都要重推/rebase、且改动藏在 fork 的历史里看不出来）不划算；
  fork 还叫 `sing-box`，会碰到上游 LICENSE 里的**名称限制条款**（未经同意不得在衍生作品中
  使用 sing-box 名称）。补丁放在本仓库，改动公开可见，升级只需改 `SING_BOX_VERSION`。
- **升级内核的流程**：

  ```sh
  # 1) 改 build.yml 的 SING_BOX_VERSION
  # 2) 本地验证补丁仍然干净
  git clone --depth 1 --branch <新 tag> https://github.com/SagerNet/sing-box.git /tmp/sb
  git -C /tmp/sb apply --check "$PWD/patches/sing-box-0001-platform-log-disable-colors.patch"
  ```

  打不上就按新上下文重做补丁；CI 里 `git apply` 失败会**当场**失败，不会静默用未打补丁的
  内核发版；打完还会 grep 自检那一行确实落在 `log/observable.go` 里。
