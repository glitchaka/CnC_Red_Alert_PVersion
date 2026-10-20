#include <ra/Engine.h>
#include <ra/RTS.h>
#include "EmbeddedAssets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct ClickFx { float x{0}; float y{0}; float life{0}; };
enum class Screen { Splash, Menu, Game };

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
bool inside(float x,float y,float l,float t,float r,float b){ return x>=l&&x<=r&&y>=t&&y<=b; }

ra::Texture makeIndexed(ra::Engine& engine, int w, int h,
                        const unsigned char* palette, std::size_t paletteCount,
                        const unsigned char* indices)
{
    std::vector<unsigned char> rgba(static_cast<std::size_t>(w*h*4));
    for (int i=0;i<w*h;++i) {
        const std::size_t p = std::min<std::size_t>(indices[i], paletteCount-1);
        rgba[static_cast<std::size_t>(i)*4+0] = palette[p*4+0];
        rgba[static_cast<std::size_t>(i)*4+1] = palette[p*4+1];
        rgba[static_cast<std::size_t>(i)*4+2] = palette[p*4+2];
        rgba[static_cast<std::size_t>(i)*4+3] = palette[p*4+3];
    }
    return engine.createTextureRGBA(rgba.data(), w, h);
}

template<std::size_t N>
ra::Texture makeAsset(ra::Engine& e,int w,int h,const unsigned char (&pal)[N][4],const unsigned char* idx)
{
    return makeIndexed(e,w,h,&pal[0][0],N,idx);
}

} // namespace

class ClickerRTS final : public ra::Game {
public:
    void onStart(ra::Engine& e) override
    {
        using namespace click_assets;
        grass_      = makeAsset(e,grass_w,grass_h,grass_palette,grass_indices);
        dirt_       = makeAsset(e,dirt_w,dirt_h,dirt_palette,dirt_indices);
        dirtTop_    = makeAsset(e,dirt_top_w,dirt_top_h,dirt_top_palette,dirt_top_indices);
        dirtBottom_ = makeAsset(e,dirt_bottom_w,dirt_bottom_h,dirt_bottom_palette,dirt_bottom_indices);
        dirtLeft_   = makeAsset(e,dirt_left_w,dirt_left_h,dirt_left_palette,dirt_left_indices);
        dirtRight_  = makeAsset(e,dirt_right_w,dirt_right_h,dirt_right_palette,dirt_right_indices);
        hill_       = makeAsset(e,hill_w,hill_h,hill_palette,hill_indices);
        wall_       = makeAsset(e,wall_w,wall_h,wall_palette,wall_indices);
        wood_       = makeAsset(e,wood_w,wood_h,wood_palette,wood_indices);
        unit_       = makeAsset(e,unit_w,unit_h,unit_palette,unit_indices);
        e.setWindowTitle("Click Command - Loading");
    }

    void onUpdate(ra::Engine& e,float dt) override
    {
        if(screen_==Screen::Splash){
            splash_+=dt;
            if(splash_>0.8f||e.keyPressed(ra::Key::Enter)||e.mousePressed(ra::MouseButton::Left)){
                screen_=Screen::Menu; e.setWindowTitle("Click Command");
            }
            return;
        }
        if(screen_==Screen::Menu){ updateMenu(e); return; }
        updateGame(e,dt);
    }

    void onRender(ra::Engine& e) override
    {
        if(screen_==Screen::Splash) renderSplash(e);
        else if(screen_==Screen::Menu) renderMenu(e);
        else renderGame(e);
    }

private:
    static constexpr float Tile = 32.0f;

    void updateMenu(ra::Engine& e)
    {
        if(e.keyPressed(ra::Key::Escape)){ e.quit(); return; }
        const float w=(float)e.windowWidth(), h=(float)e.windowHeight();
        const float bx=w*0.5f-210.0f, by=h*0.58f;
        if(e.keyPressed(ra::Key::Enter)||(e.mousePressed(ra::MouseButton::Left)&&inside(e.mouseX(),e.mouseY(),bx,by,bx+420,by+64))) startGame(e);
    }

    void startGame(ra::Engine& e)
    {
        screen_=Screen::Game; resources_=0; world_.clear(); clickFx_.clear();
        zoom_=1.0f; drag_=false;
        cameraX_=baseX_-420.0f; cameraY_=baseY_-300.0f;
        spawn(baseX_+80,baseY_+125,false);
        spawn(baseX_-55,baseY_+130,false);
        spawn(baseX_+150,baseY_+80,false);
        e.setWindowTitle("Click Command - RTS Clicker");
    }

    int nextUnitCost() const
    {
        const int bought=std::max(0,(int)world_.units().size()-3);
        return (int)std::ceil(baseCost_*std::pow(costGrowth_,bought));
    }

    void spawn(float x,float y,bool select)
    {
        auto id=world_.spawnUnit({x,y},0,96.0f);
        world_.setAutonomous(id,true,145.0f);
        if(select){ world_.clearSelection(); if(auto* u=world_.find(id)) u->selected=true; }
    }

    void buyUnit()
    {
        const int cost=nextUnitCost(); if(resources_<cost) return;
        resources_-=cost;
        const int n=(int)world_.units().size();
        const float a=(float)n*0.9f;
        spawn(baseX_+std::cos(a)*145.0f,baseY_+std::sin(a)*115.0f,true);
    }

    void updateGame(ra::Engine& e,float dt)
    {
        if(e.keyPressed(ra::Key::Escape)){ screen_=Screen::Menu; e.setWindowTitle("Click Command"); return; }
        const float panelX=hudX(e), mx=e.mouseX(), my=e.mouseY();

        const float wh=e.mouseWheelY();
        if(wh!=0) zoom_=clampf(zoom_+wh*0.12f,0.65f,2.6f);
        if(e.keyDown(ra::Key::Q)) zoom_=clampf(zoom_-dt,0.65f,2.6f);
        if(e.keyDown(ra::Key::E)) zoom_=clampf(zoom_+dt,0.65f,2.6f);

        float dx=0,dy=0;
        if(e.keyDown(ra::Key::A)||e.keyDown(ra::Key::Left)) dx-=1;
        if(e.keyDown(ra::Key::D)||e.keyDown(ra::Key::Right)) dx+=1;
        if(e.keyDown(ra::Key::W)||e.keyDown(ra::Key::Up)) dy-=1;
        if(e.keyDown(ra::Key::S)||e.keyDown(ra::Key::Down)) dy+=1;
        if(mx<10) dx-=1; if(mx<panelX&&mx>panelX-10) dx+=1;
        if(my<10) dy-=1; if(my>(float)e.windowHeight()-10) dy+=1;
        const float speed=560.0f/zoom_*dt;
        cameraX_+=dx*speed; cameraY_+=dy*speed; clampCamera(e);

        const float buyX=panelX+20,buyY=150,buyW=(float)e.windowWidth()-panelX-40;
        if(e.mousePressed(ra::MouseButton::Left)){
            if(inside(mx,my,buyX,buyY,buyX+buyW,buyY+72)) buyUnit();
            else if(mx<panelX){ drag_=true; sx_=ex_=mx; sy_=ey_=my; }
        }
        if(drag_&&e.mouseDown(ra::MouseButton::Left)){ ex_=mx; ey_=my; }
        if(drag_&&e.mouseReleased(ra::MouseButton::Left)){
            ex_=mx; ey_=my;
            const float ddx=ex_-sx_, ddy=ey_-sy_;
            if(ddx*ddx+ddy*ddy<=36){
                ++resources_; clickFx_.push_back({mx,my,0.65f});
                world_.selectPoint({screenToWorldX(mx),screenToWorldY(my)},22.0f/zoom_,false);
            } else {
                world_.selectRect({screenToWorldX(std::min(sx_,ex_)),screenToWorldY(std::min(sy_,ey_)),screenToWorldX(std::max(sx_,ex_)),screenToWorldY(std::max(sy_,ey_))},false);
            }
            drag_=false;
        }
        if(e.mousePressed(ra::MouseButton::Right)&&mx<panelX) world_.commandMove({screenToWorldX(mx),screenToWorldY(my)},30.0f);

        world_.update(dt);
        for(auto& f:clickFx_){f.life-=dt;f.y-=28*dt;}
        clickFx_.erase(std::remove_if(clickFx_.begin(),clickFx_.end(),[](const ClickFx& f){return f.life<=0;}),clickFx_.end());
    }

    void renderSplash(ra::Engine& e)
    {
        const float w=(float)e.windowWidth(),h=(float)e.windowHeight();
        e.clear({8,11,12,255});
        e.drawText(w*0.5f-58,h*0.42f,"CLICK COMMAND",{245,238,205,255});
        e.drawText(w*0.5f-72,h*0.47f,"MYSTIC WOODS BUILD",{150,180,155,255});
        e.fillRect({w*0.5f-180,h*0.57f,360,12},{32,43,39,255});
        e.fillRect({w*0.5f-180,h*0.57f,360*clampf(splash_/0.8f,0,1),12},{83,160,59,255});
    }

    void renderMenu(ra::Engine& e)
    {
        const float w=(float)e.windowWidth(),h=(float)e.windowHeight();
        e.clear({10,15,13,255});
        drawPreviewStrip(e,w*0.5f-256,h*0.18f);
        e.drawText(w*0.5f-60,h*0.43f,"CLICK COMMAND",{244,236,202,255});
        e.drawText(w*0.5f-132,h*0.48f,"RTS + CLICKER ECONOMY",{175,195,175,255});
        const float bx=w*0.5f-210,by=h*0.58f;
        const bool hot=inside(e.mouseX(),e.mouseY(),bx,by,bx+420,by+64);
        e.fillRect({bx,by,420,64},hot?ra::Color{60,118,66,255}:ra::Color{44,92,52,255});
        e.drawRect({bx,by,420,64},{132,165,134,255});
        e.drawText(bx+172,by+28,"START GAME",{245,245,225,255});
        e.drawText(w*0.5f-115,by+100,"ENTER Start    ESC Exit",{135,151,141,255});
    }

    void drawPreviewStrip(ra::Engine& e,float x,float y)
    {
        for(int row=0;row<5;++row) for(int col=0;col<16;++col){
            ra::Texture* t=&grass_;
            if(row==2) t=&dirt_;
            else if(row==1) t=&dirtTop_;
            else if(row==3) t=&dirtBottom_;
            e.drawTexture(*t,{x+col*32.0f,y+row*32.0f,32,32});
        }
    }

    void renderGame(ra::Engine& e)
    {
        const float viewW=hudX(e),viewH=(float)e.windowHeight();
        e.clear({7,9,8,255});
        drawTerrain(e,viewW,viewH);
        drawBuildings(e);
        drawUnits(e,viewW,viewH);
        if(drag_){ const float l=std::min(sx_,ex_),r=std::max(sx_,ex_),t=std::min(sy_,ey_),b=std::max(sy_,ey_); e.drawRect({l,t,r-l,b-t},{114,240,118,255}); }
        for(const auto& f:clickFx_) e.drawText(f.x-5,f.y,"+1",{250,219,70,255});
        drawHud(e,viewW,viewH);
    }

    void drawTerrain(ra::Engine& e,float viewW,float viewH)
    {
        const int firstX=std::max(0,(int)std::floor(cameraX_/Tile)-1);
        const int firstY=std::max(0,(int)std::floor(cameraY_/Tile)-1);
        const int lastX=std::min(mapW_-1,firstX+(int)(viewW/(Tile*zoom_))+4);
        const int lastY=std::min(mapH_-1,firstY+(int)(viewH/(Tile*zoom_))+4);
        for(int ty=firstY;ty<=lastY;++ty){
            for(int tx=firstX;tx<=lastX;++tx){
                ra::Texture* tex=&grass_;
                const bool hroad=(ty>=30&&ty<=33&&tx>=3&&tx<mapW_-3);
                const bool vroad=(tx>=54&&tx<=57&&ty>=8&&ty<mapH_-4);
                if(hroad){ if(ty==30) tex=&dirtTop_; else if(ty==33) tex=&dirtBottom_; else tex=&dirt_; }
                if(vroad){ if(tx==54) tex=&dirtLeft_; else if(tx==57) tex=&dirtRight_; else tex=&dirt_; }
                drawMapTile(e,*tex,tx,ty);
                if(!hroad&&!vroad&&((tx*19+ty*37)%113==0||(tx*31+ty*11)%181==0)) drawMapTile(e,hill_,tx,ty);
            }
        }
    }

    void drawMapTile(ra::Engine& e,const ra::Texture& tex,int tx,int ty)
    {
        const float s=Tile*zoom_;
        e.drawTexture(tex,{(tx*Tile-cameraX_)*zoom_,(ty*Tile-cameraY_)*zoom_,s,s});
    }

    void drawBuilding(ra::Engine& e,int tx,int ty,int w,int h)
    {
        for(int y=0;y<h;++y) for(int x=0;x<w;++x){
            const bool edge=x==0||y==0||x==w-1||y==h-1;
            drawMapTile(e,edge?wall_:wood_,tx+x,ty+y);
        }
    }

    void drawBuildings(ra::Engine& e)
    {
        drawBuilding(e,24,18,6,5);
        drawBuilding(e,34,20,5,4);
        drawBuilding(e,18,22,4,4);
        drawBuilding(e,42,15,3,3);
        drawBuilding(e,42,27,3,3);
        const float bx=(baseX_-cameraX_)*zoom_,by=(baseY_-cameraY_)*zoom_;
        e.drawText(bx-20,by-72*zoom_,"BASE",{244,239,206,255});
    }

    void drawUnits(ra::Engine& e,float viewW,float viewH)
    {
        const float size=30.0f*zoom_;
        for(const auto& u:world_.units()){
            const float x=(u.position.x-cameraX_)*zoom_, y=(u.position.y-cameraY_)*zoom_;
            if(x<-size||y<-size||x>viewW+size||y>viewH+size) continue;
            if(u.selected){
                const float tx=(u.destination.x-cameraX_)*zoom_,ty=(u.destination.y-cameraY_)*zoom_;
                e.drawLine(x,y,tx,ty,{81,205,93,150});
                e.drawRect({x-size*0.62f,y-size*0.62f,size*1.24f,size*1.24f},{90,240,104,255});
            }
            e.drawTexture(unit_,{x-size*0.5f,y-size*0.5f,size,size});
        }
    }

    void drawHud(ra::Engine& e,float x,float h)
    {
        const float pw=(float)e.windowWidth()-x;
        e.fillRect({x,0,pw,h},{16,21,18,255});
        e.drawText(x+20,20,"CLICK COMMAND",{244,237,207,255});
        e.drawText(x+20,54,"RESOURCES",{155,172,160,255});
        e.drawText(x+20,78,std::to_string(resources_),{244,210,69,255});
        e.drawText(x+20,104,"CLICK MAP: +1",{125,146,132,255});

        const int cost=nextUnitCost(); const bool can=resources_>=cost;
        const float bx=x+20,by=150,bw=pw-40;
        e.fillRect({bx,by,bw,72},can?ra::Color{45,95,53,255}:ra::Color{48,53,49,255});
        e.drawRect({bx,by,bw,72},{104,132,110,255});
        e.drawTexture(unit_,{bx+14,by+12,42,42});
        e.drawText(bx+70,by+18,"BASIC UNIT",{239,239,217,255});
        e.drawText(bx+70,by+44,("COST: "+std::to_string(cost)).c_str(),can?ra::Color{244,210,69,255}:ra::Color{130,135,130,255});

        e.drawText(x+20,248,("UNITS: "+std::to_string(world_.units().size())).c_str(),{190,201,193,255});
        e.drawText(x+20,272,("SELECTED: "+std::to_string(world_.selectedCount())).c_str(),{190,201,193,255});
        e.drawText(x+20,296,"UNIT AI: ACTIVE",{95,214,110,255});
        e.drawText(x+20,320,"COST GROWTH: 18%",{150,165,154,255});
        e.drawText(x+20,368,"LMB       +1 resource",{158,174,163,255});
        e.drawText(x+20,392,"DRAG LMB  select",{158,174,163,255});
        e.drawText(x+20,416,"RMB       move",{158,174,163,255});
        e.drawText(x+20,440,"WASD      camera",{158,174,163,255});
        e.drawText(x+20,464,"WHEEL     zoom",{158,174,163,255});

        const float mx=x+20,my=h-180,mw=pw-40,mh=120;
        e.drawText(mx,my-20,"MINIMAP",{220,224,210,255});
        e.fillRect({mx,my,mw,mh},{50,95,51,255});
        for(const auto& u:world_.units()){
            e.fillRect({mx+u.position.x/worldW_*mw-1,my+u.position.y/worldH_*mh-1,3,3},u.selected?ra::Color{88,245,104,255}:ra::Color{238,229,182,255});
        }
        e.drawRect({mx+cameraX_/worldW_*mw,my+cameraY_/worldH_*mh,std::max(8.0f,(hudX(e)/zoom_)/worldW_*mw),std::max(8.0f,((float)e.windowHeight()/zoom_)/worldH_*mh)},{240,240,220,255});
    }

    float hudX(const ra::Engine& e) const { return (float)e.windowWidth()-320.0f; }
    float screenToWorldX(float x) const { return cameraX_+x/zoom_; }
    float screenToWorldY(float y) const { return cameraY_+y/zoom_; }
    void clampCamera(const ra::Engine& e){
        const float vw=hudX(e)/zoom_,vh=(float)e.windowHeight()/zoom_;
        cameraX_=clampf(cameraX_,0,std::max(0.0f,worldW_-vw));
        cameraY_=clampf(cameraY_,0,std::max(0.0f,worldH_-vh));
    }

    Screen screen_{Screen::Splash}; float splash_{0}; int resources_{0};
    ra::rts::World world_{{0,0,4096,2560}}; std::vector<ClickFx> clickFx_;
    float cameraX_{0};
    float cameraY_{0};
    float zoom_{1}; bool drag_{false}; float sx_{0},sy_{0},ex_{0},ey_{0};
    static constexpr int mapW_=128,mapH_=80;
    const float worldW_=mapW_*Tile,worldH_=mapH_*Tile;
    const float baseX_=26*Tile,baseY_=20*Tile;
    const int baseCost_=10; const double costGrowth_=1.18;
    ra::Texture grass_,dirt_,dirtTop_,dirtBottom_,dirtLeft_,dirtRight_,hill_,wall_,wood_,unit_;
};

int main()
{
    ra::EngineConfig c; c.title="Click Command"; c.width=1440; c.height=900; c.resizable=true; c.vsync=true;
    try { ra::Engine e(c); ClickerRTS game; return e.run(game); }
    catch(const std::exception& ex){ std::fprintf(stderr,"Startup failed: %s\n",ex.what()); return 1; }
}
