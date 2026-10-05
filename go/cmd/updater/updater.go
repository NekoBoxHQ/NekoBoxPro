package main

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/jedisct1/go-minisign"
)

// minisign 发布公钥（编译进二进制的信任锚）。
//
// 私钥（nekobox.key）已由本项目生成器产出并保管于发布者本机，
// 其内容需配置在 GitHub Secret：NEKO_MINISIGN_SECRET_KEY。
// 公钥即下方常量（minisign .pub 文件第二行的 base64 串）。
// 签名发布链路：CI tag 发布时对每个 release 资产生成 <资产>.minisig，
// updater 在解压前验签；任何一步失败都拒绝更新（fail-closed）。
const minisignPublicKey = "RWQJ/40yF6iTrj0ygW9w+I+3Ol+yXPDNaFw6AhjoPByVoayuurfmZqxl"

// maxExtractSize 是解压内容总大小上限（2 GiB），防止 zip bomb / tar bomb。
const maxExtractSize = 2 << 30

func Updater() {
	pre_cleanup := func() {
		if runtime.GOOS == "linux" {
			os.RemoveAll("./usr")
		}
		os.RemoveAll("./nekobox_update")
	}

	// find update package
	var updatePackagePath string
	if len(os.Args) == 2 && Exist(os.Args[1]) {
		updatePackagePath = os.Args[1]
	} else if Exist("./nekobox_update.zip") {
		updatePackagePath = "./nekobox_update.zip"
	} else if Exist("./nekobox_update.tar.gz") {
		updatePackagePath = "./nekobox_update.tar.gz"
	} else {
		log.Fatalln("no update")
	}
	log.Println("updating from", updatePackagePath)

	// verify minisign signature BEFORE extracting anything
	if err := verifyUpdateSignature(updatePackagePath); err != nil {
		MessageBoxPlain("NekoGui Updater", "Update rejected: "+err.Error())
		log.Fatalln("signature verification failed:", err)
	}
	log.Println("update signature verified")

	// extract update package (safe extraction)
	pre_cleanup()
	if strings.HasSuffix(updatePackagePath, ".zip") {
		err := safeExtractZip(updatePackagePath, "./nekobox_update")
		if err != nil {
			log.Fatalln(err.Error())
		}
	} else if strings.HasSuffix(updatePackagePath, ".tar.gz") {
		err := safeExtractTarGz(updatePackagePath, "./nekobox_update")
		if err != nil {
			log.Fatalln(err.Error())
		}
	}

	// remove old file
	removeAll("./*.dll")
	removeAll("./*.dmp")

	// update move
	var updateSrc string
	if runtime.GOOS == "windows" {
		updateSrc = "./nekobox_update"
	} else {
		updateSrc = "./nekobox_update/nekobox"
	}
	err := Mv(updateSrc, "./")
	if err != nil {
		MessageBoxPlain("NekoGui Updater", "Update failed. Please close the running instance and run the updater again.\n\n"+err.Error())
		log.Fatalln(err.Error())
	}

	os.RemoveAll("./nekobox_update")
	os.RemoveAll("./nekobox_update.zip")
	os.RemoveAll("./nekobox_update.tar.gz")

	// nekoray -> nekobox
	os.Remove("./nekoray.exe")
	os.Remove("./nekoray.png")
	os.Remove("./nekoray_core.exe")
}

// verifyUpdateSignature 在解压前用内置公钥校验更新包的 minisign 签名。
// 签名文件约定为 <package>.minisig（由发布方随 release 一并发布）。
// 任何一步失败（含公钥未配置、签名文件缺失、验签失败）都会拒绝更新。
func verifyUpdateSignature(pkgPath string) error {
	if strings.HasPrefix(minisignPublicKey, "RWS_PLACEHOLDER") {
		return fmt.Errorf("发布链未配置签名公钥（updater 内置公钥仍为占位值），已拒绝无签名更新")
	}
	sigPath := pkgPath + ".minisig"
	if !Exist(sigPath) {
		return fmt.Errorf("缺少签名文件 %s，已拒绝无签名更新", sigPath)
	}
	pk, err := minisign.NewPublicKey(minisignPublicKey)
	if err != nil {
		return fmt.Errorf("加载内置公钥失败: %w", err)
	}
	sig, err := minisign.NewSignatureFromFile(sigPath)
	if err != nil {
		return fmt.Errorf("加载签名文件失败: %w", err)
	}
	ok, err := pk.VerifyFromFile(pkgPath, sig)
	if err != nil {
		return fmt.Errorf("验签失败: %w", err)
	}
	if !ok {
		return fmt.Errorf("验签失败: 签名与文件内容不匹配")
	}
	return nil
}

// safeExtractZip 安全解压 zip：拒绝符号链接/特殊文件、绝对路径与路径逃逸，
// 并限制解压总大小。
func safeExtractZip(pkgPath, destDir string) error {
	r, err := zip.OpenReader(pkgPath)
	if err != nil {
		return err
	}
	defer r.Close()

	var total int64
	for _, f := range r.File {
		if f.Mode()&os.ModeSymlink != 0 || f.Mode()&os.ModeIrregular != 0 {
			return fmt.Errorf("拒绝特殊文件（symlink/设备等）: %s", f.Name)
		}
		if err := checkSafePath(f.Name); err != nil {
			return err
		}
		dest := filepath.Join(destDir, filepath.FromSlash(f.Name))
		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(dest, 0755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(dest), 0755); err != nil {
			return err
		}
		rc, err := f.Open()
		if err != nil {
			return err
		}
		w, err := os.OpenFile(dest, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, f.Mode().Perm())
		if err != nil {
			rc.Close()
			return err
		}
		n, copyErr := io.Copy(w, rc)
		total += n
		rcErr := rc.Close()
		wErr := w.Close()
		if copyErr != nil {
			return copyErr
		}
		if rcErr != nil {
			return rcErr
		}
		if wErr != nil {
			return wErr
		}
		if total > maxExtractSize {
			return fmt.Errorf("解压内容超过上限 %d 字节，已中止（疑似压缩炸弹）", maxExtractSize)
		}
	}
	return nil
}

// safeExtractTarGz 安全解压 tar.gz：只允许普通文件与目录，拒绝
// 符号链接/硬链接/设备等特殊类型，拒绝绝对路径与路径逃逸，限制解压总大小。
func safeExtractTarGz(pkgPath, destDir string) error {
	f, err := os.Open(pkgPath)
	if err != nil {
		return err
	}
	defer f.Close()
	gz, err := gzip.NewReader(f)
	if err != nil {
		return err
	}
	defer gz.Close()

	tr := tar.NewReader(gz)
	var total int64
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		switch hdr.Typeflag {
		case tar.TypeDir, tar.TypeReg, tar.TypeRegA:
			// 允许
		default:
			return fmt.Errorf("拒绝特殊文件（symlink/hardlink/设备等）: %s", hdr.Name)
		}
		if err := checkSafePath(hdr.Name); err != nil {
			return err
		}
		dest := filepath.Join(destDir, filepath.FromSlash(hdr.Name))
		if hdr.Typeflag == tar.TypeDir {
			if err := os.MkdirAll(dest, 0755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(dest), 0755); err != nil {
			return err
		}
		w, err := os.OpenFile(dest, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, os.FileMode(hdr.Mode)&0777)
		if err != nil {
			return err
		}
		n, copyErr := io.Copy(w, tr)
		total += n
		wErr := w.Close()
		if copyErr != nil {
			return copyErr
		}
		if wErr != nil {
			return wErr
		}
		if total > maxExtractSize {
			return fmt.Errorf("解压内容超过上限 %d 字节，已中止（疑似压缩炸弹）", maxExtractSize)
		}
	}
	return nil
}

// checkSafePath 校验归档条目路径：拒绝绝对路径、"."、".." 与上级逃逸。
func checkSafePath(name string) error {
	norm := strings.ReplaceAll(name, "\\", "/")
	if strings.HasPrefix(norm, "/") || filepath.IsAbs(name) {
		return fmt.Errorf("拒绝绝对路径: %s", name)
	}
	for _, part := range strings.Split(norm, "/") {
		if part == ".." {
			return fmt.Errorf("拒绝路径逃逸: %s", name)
		}
	}
	return nil
}

func Exist(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func FindExist(paths []string) string {
	for _, path := range paths {
		if Exist(path) {
			return path
		}
	}
	return ""
}

func Mv(src, dst string) error {
	s, err := os.Stat(src)
	if err != nil {
		return err
	}
	if s.IsDir() {
		es, err := os.ReadDir(src)
		if err != nil {
			return err
		}
		for _, e := range es {
			err = Mv(filepath.Join(src, e.Name()), filepath.Join(dst, e.Name()))
			if err != nil {
				return err
			}
		}
	} else {
		err = os.MkdirAll(filepath.Dir(dst), 0755)
		if err != nil {
			return err
		}
		// Windows os.Rename fails if dst exists; remove it first
		if _, statErr := os.Stat(dst); statErr == nil {
			if removeErr := os.RemoveAll(dst); removeErr != nil {
				return removeErr
			}
		}
		err = os.Rename(src, dst)
		if err != nil {
			return err
		}
	}
	return nil
}

func removeAll(glob string) {
	files, _ := filepath.Glob(glob)
	for _, f := range files {
		os.Remove(f)
	}
}
