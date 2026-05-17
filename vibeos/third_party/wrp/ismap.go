// WRP ISMAP / ChromeDP routines
package main

import (
	"bytes"
	"context"
	"fmt"
	"image"
	"image/gif"
	"image/jpeg"
	"image/png"
	"io"
	"log"
	"math"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/chromedp/cdproto/emulation"
	"github.com/chromedp/cdproto/input"
	"github.com/chromedp/cdproto/page"
	"github.com/chromedp/chromedp"
	"github.com/chromedp/chromedp/kb"
	"github.com/lithammer/shortuuid/v4"
	"github.com/tenox7/gip"
)

type cachedImg struct {
	buf bytes.Buffer
}

type cachedMap struct {
	req wrpReq
}

type wrpCache struct {
	sync.Mutex
	imgs map[string]cachedImg
	maps map[string]cachedMap
}

func (c *wrpCache) addImg(path string, buf bytes.Buffer) {
	c.Lock()
	defer c.Unlock()
	c.imgs[path] = cachedImg{buf: buf}
}

func (c *wrpCache) getImg(path string) (bytes.Buffer, bool) {
	c.Lock()
	defer c.Unlock()
	e, ok := c.imgs[path]
	if !ok {
		return bytes.Buffer{}, false
	}
	return e.buf, true
}

func (c *wrpCache) addMap(path string, req wrpReq) {
	c.Lock()
	defer c.Unlock()
	c.maps[path] = cachedMap{req: req}
}

func (c *wrpCache) getMap(path string) (wrpReq, bool) {
	c.Lock()
	defer c.Unlock()
	e, ok := c.maps[path]
	if !ok {
		return wrpReq{}, false
	}
	return e.req, true
}

func (c *wrpCache) clear() {
	c.Lock()
	defer c.Unlock()
	c.imgs = make(map[string]cachedImg)
	c.maps = make(map[string]cachedMap)
}

func chromedpStart() (context.CancelFunc, context.CancelFunc) {
	opts := append(chromedp.DefaultExecAllocatorOptions[:],
		chromedp.Flag("headless", *headless),
		chromedp.Flag("hide-scrollbars", false),
		chromedp.Flag("enable-automation", false),
		chromedp.Flag("disable-blink-features", "AutomationControlled"),
	)
	if *userAgent == "jnrbsn" {
		if ua := fetchJnrbsnUserAgent(); ua != "" {
			*userAgent = ua
		}
	}
	if *userAgent != "" {
		opts = append(opts, chromedp.UserAgent(*userAgent))
	}
	if *browserPath != "" {
		opts = append(opts, chromedp.ExecPath(*browserPath))
	}
	if *userDataDir != "" {
		opts = append(opts, chromedp.UserDataDir(*userDataDir))
	}
	actx, acncl = chromedp.NewExecAllocator(context.Background(), opts...)
	ctx, cncl = chromedp.NewContext(actx)
	return cncl, acncl
}

// Determine what actions to take
func (rq *wrpReq) actions() chromedp.Tasks {
	var t chromedp.Tasks
	// Mouse Click
	if rq.mouseX > 0 && rq.mouseY > 0 {
		log.Printf("%s Mouse Click %d,%d\n", rq.r.RemoteAddr, rq.mouseX, rq.mouseY)
		t = append(t, chromedp.MouseClickXY(float64(rq.mouseX)/float64(rq.zoom), float64(rq.mouseY)/float64(rq.zoom)))
	}
	// Buttons
	if len(rq.buttons) > 0 {
		log.Printf("%s Button %v\n", rq.r.RemoteAddr, rq.buttons)
		switch rq.buttons {
		case "Bk":
			t = append(t, chromedp.NavigateBack())
		case "St":
			t = append(t, chromedp.Stop())
		case "Re":
			t = append(t, chromedp.Reload())
		case "Bs":
			t = append(t, chromedp.KeyEvent("\b"))
		case "Rt":
			t = append(t, chromedp.KeyEvent("\r"))
		case "Up":
			t = append(t, chromedp.KeyEvent("\u0308"))
		case "Dn":
			t = append(t, chromedp.KeyEvent("\u0307"))
		case "All":
			t = append(t, chromedp.KeyEvent("a", chromedp.KeyModifiers(input.ModifierCtrl)))
		}
	}
	// Keys
	if len(rq.keys) > 0 {
		log.Printf("%s Sending Keys: %#v\n", rq.r.RemoteAddr, rq.keys)
		key := rq.keys
		switch key {
		case "Backspace":
			key = "\b"
		case "Return":
			key = "\r"
		case "Tab":
			key = kb.Tab
		case "Escape":
			key = kb.Escape
		case "Up":
			key = kb.ArrowUp
		case "Down":
			key = kb.ArrowDown
		case "Left":
			key = kb.ArrowLeft
		case "Right":
			key = kb.ArrowRight
		case "Delete":
			key = kb.Delete
		case "Home":
			t = append(t, rq.scrollAction("Home", `(() => {
				const e = document.scrollingElement || document.documentElement || document.body;
				e.scrollTop = 0;
				window.scrollTo(0, 0);
				return e.scrollTop;
			})()`))
			key = ""
		case "End":
			t = append(t, rq.scrollAction("End", `(() => {
				const e = document.scrollingElement || document.documentElement || document.body;
				e.scrollTop = Math.max(e.scrollHeight, document.body ? document.body.scrollHeight : 0);
				window.scrollTo(0, e.scrollTop);
				return e.scrollTop;
			})()`))
			key = ""
		case "PageDown":
			t = append(t, rq.scrollAction("PageDown", `(() => {
				const e = document.scrollingElement || document.documentElement || document.body;
				const dy = Math.max(300, Math.floor(window.innerHeight * 0.85));
				e.scrollTop += dy;
				window.scrollTo(0, e.scrollTop);
				return e.scrollTop;
			})()`))
			key = ""
		case "PageUp":
			t = append(t, rq.scrollAction("PageUp", `(() => {
				const e = document.scrollingElement || document.documentElement || document.body;
				const dy = Math.max(300, Math.floor(window.innerHeight * 0.85));
				e.scrollTop -= dy;
				if (e.scrollTop < 0) e.scrollTop = 0;
				window.scrollTo(0, e.scrollTop);
				return e.scrollTop;
			})()`))
			key = ""
		}
		if key != "" {
			t = append(t, chromedp.KeyEvent(key))
		}
	}
	// Navigate to URL if no actions
	if len(t) == 0 {
		log.Printf("%s Processing Navigate Request for %s\n", rq.r.RemoteAddr, rq.url)
		t = append(t, chromedp.Navigate(rq.url))
	}
	return t
}

func (rq *wrpReq) scrollAction(name, script string) chromedp.Action {
	return chromedp.ActionFunc(func(ctx context.Context) error {
		var y float64
		err := chromedp.Evaluate(script, &y).Do(ctx)
		log.Printf("%s Scroll %s y=%.0f err=%v\n", rq.r.RemoteAddr, name, y, err)
		return err
	})
}

// Navigate to the desired URL.
func (rq *wrpReq) navigate() {
	ctxErr(chromedp.Run(ctx, rq.actions()), rq.w)
}

// Handle context errors
func ctxErr(err error, w io.Writer) {
	// TODO: callers should have retry logic, perhaps create another function
	// that takes ...chromedp.Action and retries with give up
	if err == nil {
		return
	}
	log.Printf("Context error: %s", err)
	fmt.Fprintf(w, "Context error: %s<BR>\n", err)
	if err.Error() != "context canceled" {
		return
	}
	ctx, cncl = chromedp.NewContext(actx)
	log.Printf("Created new context, try again")
	fmt.Fprintln(w, "Created new context, try again")
}

func waitForRender() chromedp.ActionFunc {
	return func(ctx context.Context) error {
		timeout := *delay
		if timeout > 5*time.Second {
			timeout = 5 * time.Second
		}
		ch := make(chan struct{}, 1)
		lctx, lcancel := context.WithCancel(ctx)
		defer lcancel()
		chromedp.ListenTarget(lctx, func(ev interface{}) {
			if e, ok := ev.(*page.EventLifecycleEvent); ok && e.Name == "networkAlmostIdle" {
				select {
				case ch <- struct{}{}:
				default:
				}
			}
		})
		deadline := time.Now().Add(timeout)
		for time.Now().Before(deadline) {
			select {
			case <-ch:
				return nil
			default:
			}
			var ready bool
			if err := chromedp.Evaluate(
				`document.readyState === "complete" && Array.from(document.images).every(i => i.complete)`,
				&ready,
			).Do(ctx); err != nil {
				return nil
			}
			if ready {
				return nil
			}
			time.Sleep(100 * time.Millisecond)
		}
		return nil
	}
}

// https://github.com/chromedp/chromedp/issues/979
func chromedpCaptureScreenshot(res *[]byte, h int64) chromedp.Action {
	if res == nil {
		panic("res cannot be nil") // TODO: do not panic here, return error
	}
	if h == 0 {
		return chromedp.CaptureScreenshot(res)
	}

	return chromedp.ActionFunc(func(ctx context.Context) error {
		var err error
		*res, err = page.CaptureScreenshot().Do(ctx)
		return err
	})
}

func isScrollKey(k string) bool {
	switch k {
	case "PageDown", "PageUp", "Home", "End":
		return true
	}
	return false
}

// Capture Screenshot using CDP
func (rq *wrpReq) captureScreenshot() {
	wrpCach.clear()
	var h int64
	var pngCap []byte
	var setup chromedp.Tasks
	setup = append(setup, chromedp.Location(&rq.url))
	if rq.height == 0 {
		setup = append(setup,
			emulation.SetDeviceMetricsOverride(int64(float64(rq.width)/rq.zoom), 10, rq.zoom, false),
			chromedp.ActionFunc(func(ctx context.Context) error {
				_, _, _, _, _, s, err := page.GetLayoutMetrics().Do(ctx)
				if err == nil {
					h = int64(math.Ceil(s.Height))
				}
				return nil
			}),
		)
	} else {
		h = rq.height
	}
	chromedp.Run(ctx, setup...)
	if rq.proxy {
		rq.url = strings.Replace(rq.url, "https://", "http://", 1)
	}
	log.Printf("%s Landed on: %s, Height: %v\n", rq.r.RemoteAddr, rq.url, h)
	height := int64(float64(rq.height) / rq.zoom)
	if rq.height == 0 && h > 0 {
		height = h + 30
	}
	var captureSetup chromedp.Tasks
	if !isScrollKey(rq.keys) {
		captureSetup = append(captureSetup,
			emulation.SetDeviceMetricsOverride(int64(float64(rq.width)/rq.zoom), height, rq.zoom, false))
	}
	if isScrollKey(rq.keys) {
		captureSetup = append(captureSetup, chromedp.Sleep(50*time.Millisecond))
	} else {
		captureSetup = append(captureSetup, waitForRender())
	}
	chromedp.Run(ctx, captureSetup...)
	// Capture screenshot...
	ctxErr(chromedp.Run(ctx, chromedpCaptureScreenshot(&pngCap, rq.height)), rq.w)
	seq := shortuuid.New()
	var imgExt string
	if rq.imgType == "gip" {
		imgExt = "gif"
	} else {
		imgExt = rq.imgType
	}
	imgPath := fmt.Sprintf("/img/%s.%s", seq, imgExt)
	mapPath := fmt.Sprintf("/map/%s.map", seq)
	wrpCach.addMap(mapPath, *rq)
	var sSize string
	var iW, iH int
	switch rq.imgType {
	case "gip":
		i, err := png.Decode(bytes.NewReader(pngCap))
		if err != nil {
			log.Printf("%s Failed to decode PNG screenshot: %s\n", rq.r.RemoteAddr, err)
			fmt.Fprintf(rq.w, "<BR>Unable to decode page PNG screenshot:<BR>%s<BR>\n", err)
			return
		}
		st := time.Now()
		var gipBuf bytes.Buffer
		err = gip.Encode(&gipBuf, i, nil)
		if err != nil {
			log.Printf("%s Failed to encode GIP: %s\n", rq.r.RemoteAddr, err)
			fmt.Fprintf(rq.w, "<BR>Unable to encode GIP:<BR>%s<BR>\n", err)
			return
		}
		wrpCach.addImg(imgPath, gipBuf)
		sSize = fmt.Sprintf("%.0f KB", float32(len(gipBuf.Bytes()))/1024.0)
		iW = i.Bounds().Max.X
		iH = i.Bounds().Max.Y
		log.Printf("%s Encoded GIP image: %s, Size: %s, Res: %dx%d, Time: %vms\n", rq.r.RemoteAddr, imgPath, sSize, iW, iH, time.Since(st).Milliseconds())
	case "png":
		pngBuf := bytes.NewBuffer(pngCap)
		wrpCach.addImg(imgPath, *pngBuf)
		cfg, _, _ := image.DecodeConfig(pngBuf)
		sSize = fmt.Sprintf("%.0f KB", float32(len(pngBuf.Bytes()))/1024.0)
		iW = cfg.Width
		iH = cfg.Height
		log.Printf("%s Got PNG image: %s, Size: %s, Res: %dx%d\n", rq.r.RemoteAddr, imgPath, sSize, iW, iH)
	case "gif":
		i, err := png.Decode(bytes.NewReader(pngCap))
		if err != nil {
			log.Printf("%s Failed to decode PNG screenshot: %s\n", rq.r.RemoteAddr, err)
			fmt.Fprintf(rq.w, "<BR>Unable to decode page PNG screenshot:<BR>%s<BR>\n", err)
			return
		}
		st := time.Now()
		var gifBuf bytes.Buffer
		err = gif.Encode(&gifBuf, gifPalette(i, rq.nColors), &gif.Options{})
		if err != nil {
			log.Printf("%s Failed to encode GIF: %s\n", rq.r.RemoteAddr, err)
			fmt.Fprintf(rq.w, "<BR>Unable to encode GIF:<BR>%s<BR>\n", err)
			return
		}
		wrpCach.addImg(imgPath, gifBuf)
		sSize = fmt.Sprintf("%.0f KB", float32(len(gifBuf.Bytes()))/1024.0)
		iW = i.Bounds().Max.X
		iH = i.Bounds().Max.Y
		log.Printf("%s Encoded GIF image: %s, Size: %s, Colors: %d, Res: %dx%d, Time: %vms\n", rq.r.RemoteAddr, imgPath, sSize, rq.nColors, iW, iH, time.Since(st).Milliseconds())
	case "jpg":
		i, err := png.Decode(bytes.NewReader(pngCap))
		if err != nil {
			log.Printf("%s Failed to decode PNG screenshot: %s\n", rq.r.RemoteAddr, err)
			fmt.Fprintf(rq.w, "<BR>Unable to decode page PNG screenshot:<BR>%s<BR>\n", err)
			return
		}
		st := time.Now()
		var jpgBuf bytes.Buffer
		err = jpeg.Encode(&jpgBuf, i, &jpeg.Options{Quality: int(rq.jQual)})
		if err != nil {
			log.Printf("%s Failed to encode JPG: %s\n", rq.r.RemoteAddr, err)
			fmt.Fprintf(rq.w, "<BR>Unable to encode JPG:<BR>%s<BR>\n", err)
			return
		}
		wrpCach.addImg(imgPath, jpgBuf)
		sSize = fmt.Sprintf("%.0f KB", float32(len(jpgBuf.Bytes()))/1024.0)
		iW = i.Bounds().Max.X
		iH = i.Bounds().Max.Y
		log.Printf("%s Encoded JPG image: %s, Size: %s, Quality: %d, Res: %dx%d, Time: %vms\n", rq.r.RemoteAddr, imgPath, sSize, *defJpgQual, iW, iH, time.Since(st).Milliseconds())
	}
	if rq.proxy {
		rq.w.Header().Set("Content-Type", "text/html")
		fmt.Fprintf(rq.w, "<HTML><HEAD>%s<TITLE>%s</TITLE></HEAD><BODY BGCOLOR=\"%s\">"+
			"<A HREF=\"%s\"><IMG SRC=\"%s\" BORDER=\"0\" WIDTH=\"%d\" HEIGHT=\"%d\" ISMAP></A>"+
			"</BODY></HTML>", rq.baseTag(), rq.url, *bgColor, mapPath, imgPath, iW, iH)
	} else {
		rq.printUI(uiParams{
			bgColor:    *bgColor,
			pageHeight: fmt.Sprintf("%d PX", h),
			imgSize:    sSize,
			imgURL:     imgPath,
			mapURL:     mapPath,
			imgWidth:   iW,
			imgHeight:  iH,
		})
	}
	log.Printf("%s Done with capture for %s\n", rq.r.RemoteAddr, rq.url)
}

func mapServer(w http.ResponseWriter, r *http.Request) {
	log.Printf("%s ISMAP Request for %s [%+v]\n", r.RemoteAddr, r.URL.Path, r.URL.RawQuery)
	rq, ok := wrpCach.getMap(r.URL.Path)
	rq.r = r
	rq.w = w
	if !ok {
		fmt.Fprintf(w, "Unable to find map %s\n", r.URL.Path)
		log.Printf("Unable to find map %s\n", r.URL.Path)
		return
	}
	n, err := fmt.Sscanf(r.URL.RawQuery, "%d,%d", &rq.mouseX, &rq.mouseY)
	if err != nil || n != 2 {
		fmt.Fprintf(w, "n=%d, err=%s\n", n, err)
		log.Printf("%s ISMAP n=%d, err=%s\n", r.RemoteAddr, n, err)
		return
	}
	if k := r.URL.Query().Get("k"); k != "" {
		rq.keys = k
	}
	log.Printf("%s WrpReq from ISMAP: %+v\n", r.RemoteAddr, rq)
	if len(rq.url) < 4 {
		rq.printUI(uiParams{})
		return
	}
	rq.navigate() // TODO: if error from navigate do not capture
	if rq.proxy {
		chromedp.Run(ctx, waitForRender())
		var loc string
		chromedp.Run(ctx, chromedp.Location(&loc))
		loc = strings.Replace(loc, "https://", "http://", 1)
		http.Redirect(w, r, loc, http.StatusFound)
		return
	}
	rq.captureScreenshot()
}

// TODO: merge this with html mode IMGZ
func imgServerMap(w http.ResponseWriter, r *http.Request) {
	log.Printf("%s IMG Request for %s\n", r.RemoteAddr, r.URL.Path)
	imgBuf, ok := wrpCach.getImg(r.URL.Path)
	if !ok || imgBuf.Bytes() == nil {
		fmt.Fprintf(w, "Unable to find image %s\n", r.URL.Path)
		log.Printf("%s Unable to find image %s\n", r.RemoteAddr, r.URL.Path)
		return
	}
	switch {
	case strings.HasSuffix(r.URL.Path, ".gif"):
		w.Header().Set("Content-Type", "image/gif")
	case strings.HasSuffix(r.URL.Path, ".png"):
		w.Header().Set("Content-Type", "image/png")
	case strings.HasSuffix(r.URL.Path, ".jpg"):
		w.Header().Set("Content-Type", "image/jpeg")
	}
	w.Header().Set("Content-Length", strconv.Itoa(len(imgBuf.Bytes())))
	w.Header().Set("Cache-Control", "max-age=0")
	w.Header().Set("Expires", "-1")
	w.Header().Set("Pragma", "no-cache")
	w.Write(imgBuf.Bytes())
	w.(http.Flusher).Flush()
}
