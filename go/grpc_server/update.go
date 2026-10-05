package grpc_server

import (
	"context"
	"encoding/json"
	"fmt"
	"grpc_server/gen"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
)

var update_download_url string

// fetchLatestTagFromAtom 通过 GitHub releases.atom（RSS）获取最新发布 tag，
// 用于 API 限流（403）时的备用版本确认。
func fetchLatestTagFromAtom(client *http.Client) string {
	req, err := http.NewRequest("GET", "https://github.com/NekoBoxHQ/NekoBox/releases.atom", nil)
	if err != nil {
		return ""
	}
	req.Header.Set("User-Agent", "NekoBox")
	resp, err := client.Do(req)
	if err != nil || resp.StatusCode != http.StatusOK {
		return ""
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 1<<20))
	s := string(body)
	idx := strings.Index(s, "<entry>")
	if idx < 0 {
		idx = 0
	}
	rest := s[idx:]
	tIdx := strings.Index(rest, "<title>")
	if tIdx < 0 {
		return ""
	}
	end := strings.Index(rest[tIdx:], "</title>")
	if end < 0 {
		return ""
	}
	return strings.TrimSpace(rest[tIdx+7 : tIdx+end])
}

func (s *BaseServer) Update(ctx context.Context, in *gen.UpdateReq) (*gen.UpdateResp, error) {
	ret := &gen.UpdateResp{}

	client := neko_common.CreateProxyHttpClient(neko_common.GetCurrentInstance())

	if in.Action == gen.UpdateAction_Check { // Check update
		ctx, cancel := context.WithTimeout(ctx, time.Second*10)
		defer cancel()

		req, _ := http.NewRequestWithContext(ctx, "GET", "https://api.github.com/repos/NekoBoxHQ/NekoBox/releases", nil)
		req.Header.Set("User-Agent", "NekoBox/"+strings.TrimPrefix(neko_common.Version_neko, "nekoray-"))
		req.Header.Set("Accept", "application/vnd.github+json")
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()

		if resp.StatusCode != http.StatusOK {
			// API 限流/异常时用 releases.atom 备用确认最新版本（不受 API 限流影响）
			nowVer := strings.TrimPrefix(neko_common.Version_neko, "nekoray-")
			latestTag := fetchLatestTagFromAtom(client)
			if latestTag != "" {
				if strings.TrimPrefix(latestTag, "v") == nowVer {
					return ret, nil // 本地已是最新，无需升级
				}
				// 有新版本：直接构造发布页下载 URL（不经 api.github.com，绕开限流）
				var assetName string
				if runtime.GOOS == "windows" && runtime.GOARCH == "amd64" {
					assetName = "NekoBox-Windows64.zip"
				} else if runtime.GOOS == "linux" && runtime.GOARCH == "amd64" {
					assetName = "NekoBox-Linux64.tar.gz"
				} else if runtime.GOOS == "darwin" {
					assetName = "NekoBox-macOS-" + runtime.GOARCH + ".zip"
				}
				if assetName != "" {
					update_download_url = "https://github.com/NekoBoxHQ/NekoBox/releases/download/" + latestTag + "/" + assetName
					ret.AssetsName = assetName
					ret.DownloadUrl = update_download_url
					ret.ReleaseUrl = "https://github.com/NekoBoxHQ/NekoBox/releases/tag/" + latestTag
					return ret, nil // update
				}
				ret.Error = fmt.Sprintf("发现新版本 %s，但当前平台不受支持", latestTag)
				return ret, nil
			}
			ret.Error = fmt.Sprintf("更新服务暂时不可用（HTTP %d），可能是请求过于频繁或网络异常，请稍后重试", resp.StatusCode)
			return ret, nil
		}

		v := []struct {
			TagName string `json:"tag_name"`
			HtmlUrl string `json:"html_url"`
			Assets  []struct {
				Name               string `json:"name"`
				BrowserDownloadUrl string `json:"browser_download_url"`
			} `json:"assets"`
			Prerelease bool   `json:"prerelease"`
			Body       string `json:"body"`
		}{}
		err = json.NewDecoder(resp.Body).Decode(&v)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		nowVer := strings.TrimPrefix(neko_common.Version_neko, "nekoray-")

		var search string
		if runtime.GOOS == "windows" && runtime.GOARCH == "amd64" {
			search = "windows64"
		} else if runtime.GOOS == "linux" && runtime.GOARCH == "amd64" {
			search = "linux64"
		} else if runtime.GOOS == "darwin" {
			search = "macos-" + runtime.GOARCH
		} else {
			ret.Error = "Not official support platform"
			return ret, nil
		}

		// no update if the latest release tag equals the current version
		if len(v) > 0 && strings.TrimPrefix(v[0].TagName, "v") == nowVer {
			return ret, nil // No update
		}

		for _, release := range v {
			if len(release.Assets) > 0 {
				for _, asset := range release.Assets {
					if strings.Contains(strings.ToLower(asset.Name), search) {
						if release.Prerelease && !in.CheckPreRelease {
							continue
						}
						update_download_url = asset.BrowserDownloadUrl
						ret.AssetsName = asset.Name
						ret.DownloadUrl = asset.BrowserDownloadUrl
						ret.ReleaseUrl = release.HtmlUrl
						ret.ReleaseNote = release.Body
						ret.IsPreRelease = release.Prerelease
						return ret, nil // update
					}
				}
			}
		}
	} else { // Download update
		if update_download_url == "" {
			ret.Error = "no update url, run check update first"
			return ret, nil
		}

		req, _ := http.NewRequestWithContext(ctx, "GET", update_download_url, nil)
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()

		if resp.StatusCode != http.StatusOK {
			ret.Error = fmt.Sprintf("更新包下载失败：HTTP %d，已中止更新", resp.StatusCode)
			return ret, nil
		}

		exePath, err := os.Executable()
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		// 按实际资产格式命名更新包，供 updater 识别（zip / tar.gz）
		updateName := "nekobox_update.zip"
		if strings.HasSuffix(update_download_url, ".tar.gz") {
			updateName = "nekobox_update.tar.gz"
		}
		updatePath := filepath.Join(filepath.Dir(exePath), updateName)

		f, err := os.OpenFile(updatePath, os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0644)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		_, err = io.Copy(f, resp.Body)
		f.Close()
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		// 下载配套 minisign 签名文件（<更新包>.minisig），供 updater 解压前验签
		sigReq, _ := http.NewRequestWithContext(ctx, "GET", update_download_url+".minisig", nil)
		sigResp, err := client.Do(sigReq)
		if err != nil {
			ret.Error = "更新包签名文件下载失败：" + err.Error() + "（已中止更新，拒绝无签名更新）"
			return ret, nil
		}
		defer sigResp.Body.Close()

		if sigResp.StatusCode != http.StatusOK {
			ret.Error = fmt.Sprintf("更新包签名文件下载失败：HTTP %d（已中止更新，拒绝无签名更新）", sigResp.StatusCode)
			return ret, nil
		}

		sf, err := os.OpenFile(updatePath+".minisig", os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0644)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		_, err = io.Copy(sf, sigResp.Body)
		sf.Close()
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
	}

	return ret, nil
}
