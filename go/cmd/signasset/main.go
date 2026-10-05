// signasset 是 CI 发布签名工具：用未加密的 minisign 私钥对发布资产做预哈希签名，
// 产出 <资产>.minisig。updater 内置公钥在解压前验签（fail-closed）。
//
// 私钥文件为标准 minisign 4 行格式（untrusted comment / base64 payload /
// trusted comment / global sig）；本工具取第 2 行 payload 解码。
package main

import (
	"fmt"
	"os"
	"strings"

	"github.com/jedisct1/go-minisign"
)

// usage: signasset <secret-key-file> <file-to-sign>
func main() {
	if len(os.Args) < 3 {
		fmt.Fprintln(os.Stderr, "usage: signasset <secret-key-file> <file-to-sign>")
		os.Exit(2)
	}
	bin, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, "read private key:", err)
		os.Exit(1)
	}
	lines := strings.Split(strings.ReplaceAll(strings.TrimSpace(string(bin)), "\r\n", "\n"), "\n")
	if len(lines) < 2 {
		fmt.Fprintln(os.Stderr, "private key file too short")
		os.Exit(1)
	}
	sk, err := minisign.NewPrivateKey(lines[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, "parse private key:", err)
		os.Exit(1)
	}
	sig, err := sk.SignFile(os.Args[2], minisign.SignOptions{Hashed: true})
	if err != nil {
		fmt.Fprintln(os.Stderr, "sign:", err)
		os.Exit(1)
	}
	if err := os.WriteFile(os.Args[2]+".minisig", sig.Encode(), 0644); err != nil {
		fmt.Fprintln(os.Stderr, "write sig:", err)
		os.Exit(1)
	}
	fmt.Println("signed", os.Args[2])
}
