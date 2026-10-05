package main

import (
	"fmt"
	"os"

	"grpc_server"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/sagernet/sing-box/constant"
)

func main() {
	fmt.Println("sing-box:", constant.Version, "NekoBoxPro:", neko_common.Version_neko)
	fmt.Println()

	// nekobox_core
	if len(os.Args) > 1 && os.Args[1] == "nekobox" {
		neko_common.RunMode = neko_common.RunMode_NekoBox_Core
		grpc_server.RunCore(setupCore, &server{})
		return
	}

	// 官方 sing-box >= 1.11 的 cmd/sing-box 已不再提供库式 CLI 入口（boxmain.Main 移除），
	// 独立 sing-box 命令行模式不再可用，请以 "nekobox" 参数启动。
	fmt.Println("Usage: nekobox_core nekobox -port <port> -token <token>")
}
