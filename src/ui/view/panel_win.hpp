#pragma once
#include "ui/view/hud.hpp"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#if IBM
#ifndef NOMINMAX
#define NOMINMAX // the headers above and below call std::min and std::max
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#endif
#include <memory>

namespace brunner::monitor {
class Window {
    struct Vertex {float x,y,u,v,r,g,b,a;};
    struct Glyph {float x=0,y=0,advance=0;};
    Model model_;
    bool hud_=false;
    hud::State context_;
    recording::State recording_=recording::State::off;
    std::uint64_t dropped_=0;
    std::string status_; // the plugin's one status line
    std::string reason_; // the stop's short reason while Retry is offered
    std::optional<tail::View> pitch_; // the pitch card's model, when there is one
    int drag_x_=0,drag_y_=0;
    bool dragging_=false;
    XPLMWindowID window_=nullptr;
    int texture_=0;
    bool font_attempted_=false,dirty_=true;
    std::array<std::array<Glyph,97>,2> glyphs_{}; // ASCII 32-126, then the middle dot and the degree sign
    Canvas cached_;
    double painted_=-1;
    int old_l_=0,old_t_=0,old_r_=0,old_b_=0;
    std::vector<Vertex> shapes_,letters_;
    float origin_x_=0,origin_y_=0,scale_=1;
    static void draw_cb(XPLMWindowID,void* self) {static_cast<Window*>(self)->draw();}
    static int mouse_cb(XPLMWindowID,int x,int y,XPLMMouseStatus status,void* self) {
        auto& p=*static_cast<Window*>(self);
        if (p.hud_) {
            if (!p.settings.visible || !p.context_.moving) return 0;
            if (status==xplm_MouseDown) {
                p.geometry();if (y<p.old_t_-36 || y>p.old_t_) return 0;
                p.drag_x_=x;p.drag_y_=y;p.dragging_=true;
            } else if (p.dragging_) {
                p.settings.right-=x-p.drag_x_;p.settings.top-=y-p.drag_y_;
                p.drag_x_=x;p.drag_y_=y;p.dirty_=true;
                if (status==xplm_MouseUp) p.dragging_=false;
            }
            return p.dragging_ || status==xplm_MouseUp?1:0;
        }
        if (status==xplm_MouseDown) {
            p.geometry();p.dirty_=click(p.model_,(x-p.origin_x_)/p.scale_,(p.origin_y_-y)/p.scale_) || p.dirty_;
        }
        return 1;
    }
    static int wheel_cb(XPLMWindowID,int,int,int,int,void* self) {return static_cast<Window*>(self)->hud_?0:1;}
    static XPLMCursorStatus cursor_cb(XPLMWindowID,int,int,void*) {return xplm_CursorDefault;}
    static void key_cb(XPLMWindowID,char,XPLMKeyFlags,char,void*,int) {}
    void geometry() {
        int l,t,r,b;
        if (hud_) {
            int sl,st,sr,sb;XPLMGetScreenBoundsGlobal(&sl,&st,&sr,&sb);
            // The saved place is clamped as the pitch-only window was (216 tall), so a HUD kept
            // near the bottom stays there; only the taller window is lifted onto the screen.
            settings.right=std::clamp(settings.right,0,std::max(0,sr-sl-hud::window_width));
            settings.top=std::clamp(settings.top,0,std::max(0,st-sb-hud::saved_height));
            const int shown_top=std::clamp(settings.top,0,std::max(0,st-sb-hud::window_height));
            r=sr-settings.right;l=r-hud::window_width;t=st-shown_top;b=t-hud::window_height;
            XPLMSetWindowGeometry(window_,l,t,r,b);
        } else XPLMGetWindowGeometry(window_,&l,&t,&r,&b);
        if (l!=old_l_ || t!=old_t_ || r!=old_r_ || b!=old_b_) dirty_=true;
        old_l_=l;old_t_=t;old_r_=r;old_b_=b;
        if (hud_) {scale_=1;origin_x_=float(l);origin_y_=float(t);return;}
        scale_=std::min(float(r-l)/1200,float(t-b)/800);
        origin_x_=float(l)+(float(r-l)-1200*scale_)/2;
        origin_y_=float(t)-(float(t-b)-800*scale_)/2;
    }
    Vertex vertex(float x,float y,Color c,float u=0,float v=0) const {
        return {origin_x_+x*scale_,origin_y_-y*scale_,u,v,c.r,c.g,c.b,c.a};
    }
    void quad(std::vector<Vertex>& out,float x,float y,float w,float h,Color c,float u=0,float v=0,float uw=0,float vh=0) {
        out.push_back(vertex(x,y,c,u,v));out.push_back(vertex(x+w,y,c,u+uw,v));out.push_back(vertex(x+w,y+h,c,u+uw,v+vh));
        out.push_back(vertex(x,y,c,u,v));out.push_back(vertex(x+w,y+h,c,u+uw,v+vh));out.push_back(vertex(x,y+h,c,u,v+vh));
    }
    static void unbind_buffer(GLenum target) {
#if IBM
        using BindBuffer=void (APIENTRY*)(GLenum,GLuint);
        static BindBuffer bind=[] {
            BindBuffer result=nullptr;const auto address=wglGetProcAddress("glBindBuffer");
            static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;
        }();
        if (bind) bind(target,0);
#else
        glBindBuffer(target,0);
#endif
    }
    void font() {
        if (font_attempted_) return;
        font_attempted_=true;
#if IBM
        constexpr int side=1024,cell_w=64,cell_h=72;
        HDC dc=CreateCompatibleDC(nullptr);
        if (!dc) return;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=side;
        info.bmiHeader.biHeight=-side;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if (!bitmap || !pixels) {if (bitmap) DeleteObject(bitmap);DeleteDC(dc);return;}
        const auto old_bitmap=SelectObject(dc,bitmap);
        std::memset(pixels,0,side*side*4);
        SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,RGB(0,0,0));SetBkMode(dc,OPAQUE);
        bool okay=true;
        for (unsigned weight=0;weight<2;++weight) {
            HFONT face=CreateFontW(-40,0,0,0,weight?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            if (!face) {okay=false;break;}
            const auto old_font=SelectObject(dc,face);
            // Seven rows of 16 cells per weight: 2 x 7 x 72 fits the 1024 texture.
            for (unsigned i=0;i<97;++i) {
                const wchar_t ch=i<95?static_cast<wchar_t>(32+i):i==95?static_cast<wchar_t>(0x00B7):static_cast<wchar_t>(0x00B0);
                const int x=int(i%16)*cell_w,y=(int(i/16)+int(weight)*7)*cell_h;
                SIZE size{};GetTextExtentPoint32W(dc,&ch,1,&size);
                glyphs_[weight][i]={float(x),float(y),float(size.cx)};
                TextOutW(dc,x,y,&ch,1);
            }
            SelectObject(dc,old_font);DeleteObject(face);
        }
        GdiFlush();
        if (okay) {
            std::vector<unsigned char> rgba(side*side*4);
            const auto* source=static_cast<const unsigned char*>(pixels);
            for (std::size_t i=0;i<rgba.size();i+=4) {rgba[i]=255;rgba[i+1]=255;rgba[i+2]=255;rgba[i+3]=source[i];}
            XPLMGenerateTextureNumbers(&texture_,1);XPLMBindTexture2d(texture_,0);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
            unbind_buffer(0x88EC); // GL_PIXEL_UNPACK_BUFFER: pixels are CPU memory
            glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,side,side,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
            glPopClientAttrib();
        }
        SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);
#endif
    }
    void compile() {
        shapes_.clear();letters_.clear();shapes_.reserve(50000);letters_.reserve(16000);
        for (const auto& p:cached_.items) {
            if (p.kind==Primitive::Kind::rect) quad(shapes_,p.x,p.y,p.w,p.h,p.color);
            else if (p.kind==Primitive::Kind::line) {
                for (const auto point:line_triangles(p.x,p.y,p.w,p.h,p.size))
                    shapes_.push_back(vertex(point.x,point.y,p.color));
            } else if (p.kind==Primitive::Kind::triangle) {
                // Canvas::triangle already wound it as rect() is.
                shapes_.push_back(vertex(p.x,p.y,p.color));shapes_.push_back(vertex(p.w,p.h,p.color));shapes_.push_back(vertex(p.x3,p.y3,p.color));
            } else if (texture_) {
                float x=p.x;const float factor=p.size/40;
                for (std::size_t i=0;i<p.label.size();++i) {
                    const auto ch=static_cast<unsigned char>(p.label[i]);
                    unsigned index=ch<32 || ch>126?'?'-32u:ch-32u;
                    // UTF-8 C2 B7, the middle dot, and C2 B0, the degree sign, are the atlas's last two cells.
                    if (ch==0xC2 && i+1<p.label.size() && static_cast<unsigned char>(p.label[i+1])==0xB7) {index=95;++i;}
                    else if (ch==0xC2 && i+1<p.label.size() && static_cast<unsigned char>(p.label[i+1])==0xB0) {index=96;++i;}
                    const auto& g=glyphs_[p.bold?1:0][index];
                    quad(letters_,x,p.y-2*factor,64*factor,72*factor,p.color,g.x/1024,g.y/1024,64.0f/1024,72.0f/1024);
                    x+=g.advance*factor;
                }
            }
        }
    }
    static void submit(const std::vector<Vertex>& vertices,bool textured) {
        if (vertices.empty()) return;
        const auto* base=vertices.data();
        glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2,GL_FLOAT,sizeof(Vertex),&base->x);glColorPointer(4,GL_FLOAT,sizeof(Vertex),&base->r);
        if (textured) {glEnableClientState(GL_TEXTURE_COORD_ARRAY);glTexCoordPointer(2,GL_FLOAT,sizeof(Vertex),&base->u);}
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
        if (textured) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
    }
    void draw() {
        geometry();
        XPLMSetGraphicsState(0,1,0,0,1,0,0);font();
        const double now=std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        if (dirty_ || now-painted_>=0.05 || now<painted_) {
            cached_=hud_?hud::render(model_,context_,settings,recording_,dropped_,now,status_,pitch_,reason_):
                render(model_,false,now,recording::status_label(recording_,dropped_),status_);compile();dirty_=false;painted_=now;
        }
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        unbind_buffer(0x8892); // GL_ARRAY_BUFFER: vertices are CPU memory
        XPLMSetGraphicsState(0,0,0,0,1,0,0);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);submit(shapes_,false);
        if (texture_) {XPLMSetGraphicsState(0,1,0,0,1,0,0);XPLMBindTexture2d(texture_,0);submit(letters_,true);}
        glPopClientAttrib();
        if (!texture_) for (const auto& p:cached_.items) if (p.kind==Primitive::Kind::text) {
            float c[]{p.color.r,p.color.g,p.color.b};
            std::string text=p.label; // X-Plane's font: the middle dot as a hyphen, the degree sign as " deg"
            for (auto at=text.find("\xC2\xB7");at!=std::string::npos;at=text.find("\xC2\xB7")) text.replace(at,2,"-");
            for (auto at=text.find("\xC2\xB0");at!=std::string::npos;at=text.find("\xC2\xB0")) text.replace(at,2," deg");
            XPLMDrawString(c,int(origin_x_+p.x*scale_),int(origin_y_-(p.y+p.size)*scale_),text.c_str(),nullptr,xplmFont_Proportional);
        }
    }
public:
    hud::Settings settings;
    explicit Window(bool hud=false):hud_(hud) {}
    // Every display must receive live recorder state; a default of 'off' can
    // silently misreport a healthy logger when a caller forgets to supply it.
    void observe(const Frame& f,recording::State recording,std::uint64_t dropped) {
        model_.observe(f);if (hud_) context_.observe(f,settings);recording_=recording;dropped_=dropped;
    }
    void set_status(const std::string& status,const std::string& reason={}) {
        if (status!=status_ || reason!=reason_) {status_=status;reason_=reason;dirty_=true;}
    }
    // The pitch card's model; redrawn with the rest every 0.05 s.
    void observe_pitch(const std::optional<tail::View>& view) {pitch_=view;}
    void pin() {context_.pinned=!context_.pinned;dirty_=true;}
    bool pinned() const {return context_.pinned;}
    void move() {context_.moving=!context_.moving;dragging_=false;dirty_=true;}
    void show_hud() {if (!window_) toggle();settings.visible=1;}
    void refresh() {dirty_=true;}
    bool visible() const {return window_ && XPLMGetWindowIsVisible(window_) && (!hud_ || settings.visible);}

    void toggle() {
        if (!window_) {
            int l,t,r,b;XPLMGetScreenBoundsGlobal(&l,&t,&r,&b);
            const int width=hud_?hud::window_width:std::min(1200,r-l-80),height=hud_?hud::window_height:std::min(800,t-b-120);
            XPLMCreateWindow_t p{};p.structSize=sizeof(p);p.left=l+40;p.right=p.left+width;p.top=t-70;p.bottom=p.top-height;
            if (hud_) {p.right=r-settings.right;p.left=p.right-width;p.top=t-settings.top;p.bottom=p.top-height;}
            p.visible=1;p.drawWindowFunc=draw_cb;p.handleMouseClickFunc=mouse_cb;p.handleRightClickFunc=mouse_cb;
            p.handleMouseWheelFunc=wheel_cb;p.handleCursorFunc=cursor_cb;p.handleKeyFunc=key_cb;p.refcon=this;
            p.layer=hud_?xplm_WindowLayerFlightOverlay:xplm_WindowLayerFloatingWindows;
            p.decorateAsFloatingWindow=hud_?xplm_WindowDecorationNone:xplm_WindowDecorationRoundRectangle;
            window_=XPLMCreateWindowEx(&p);
            if (!window_) throw std::runtime_error("Could not create force monitor");
            XPLMSetWindowPositioningMode(window_,xplm_WindowPositionFree,-1);
            if (!hud_) XPLMSetWindowResizingLimits(window_,900,600,1800,1200);
            XPLMSetWindowTitle(window_,"Brunner Direct - Force monitor");
        } else if (hud_) settings.visible=!settings.visible;
        else XPLMSetWindowIsVisible(window_,!XPLMGetWindowIsVisible(window_));
        dirty_=true;
    }
    void close() {
        if (window_) {XPLMDestroyWindow(window_);window_=nullptr;}
        // OpenGL resources are released only from a rendering context. The
        // single font atlas survives disable/re-enable for the process lifetime.
        model_.clear();context_=hud::State{};pitch_.reset();dirty_=true;
    }
};
}
