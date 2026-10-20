#include <ra/Engine.h>
#include <ra/Net.h>
#include <ra/RTS.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#endif

namespace {

constexpr std::uint16_t GamePort = 46000;
constexpr float Tile = 32.0f;
constexpr int MapW = 128;
constexpr int MapH = 80;
constexpr const char* Discover = "CC_DISCOVER_6";
constexpr const char* Welcome = "CC_WELCOME_6";

enum class Screen { Splash, Menu, Lobby, Game, Editor };
enum class NetMode { Offline, Host, Client };
enum class UnitKind { Worker=0, Guerrilla, Soldier, Infantry, Sniper, Cannoneer, Air };
enum class BuildingKind { Base=0, Barracks, Hangar, Silo, Crop, Academy, Training, None=99 };
enum class UpgradeKind { Air=0, Production, Construction, LateBarracks, Barracks, Workers };
enum class Sheet { Grass=0, Plains, Decor, Fences, Walls, Flooring, Wooden, Chest };

struct ClickFx { float x{}, y{}, life{}; };
struct Building {
    int id{};
    BuildingKind kind{BuildingKind::Base};
    int team{};
    float x{}, y{};
    float health{300}, maxHealth{300};
    float buildLeft{};
    float cropTimer{};
    bool cropReady{};
};
struct Upgrades { bool air{}, production{}, construction{}, lateBarracks{}, barracks{}, workers{}; };
struct MapCell { int sheet{-1}, sx{}, sy{}; };
struct PaletteItem { Sheet sheet; int sx, sy; const char* name; };

float clampf(float v,float lo,float hi){return std::max(lo,std::min(v,hi));}
bool inside(float x,float y,float l,float t,float r,float b){return x>=l&&x<=r&&y>=t&&y<=b;}
ra::Rect tile16(int x,int y){return{x*16.0f,y*16.0f,16.0f,16.0f};}
ra::Rect frame48(int x,int y){return{x*48.0f,y*48.0f,48.0f,48.0f};}
ra::Rect frame32(int x,int y){return{x*32.0f,y*32.0f,32.0f,32.0f};}

const char* buildingName(BuildingKind k){
    switch(k){
        case BuildingKind::Base:return"BASE"; case BuildingKind::Barracks:return"BARRACKS";
        case BuildingKind::Hangar:return"HANGAR"; case BuildingKind::Silo:return"SILO";
        case BuildingKind::Crop:return"CROP"; case BuildingKind::Academy:return"ACADEMY";
        case BuildingKind::Training:return"TRAINING FIELD"; default:return"BUILDING";
    }
}

std::string trim(std::string s){while(!s.empty()&&(s.back()=='\r'||s.back()=='\n'||s.back()==' '||s.back()=='\t'))s.pop_back();std::size_t i=0;while(i<s.size()&&(s[i]==' '||s[i]=='\t'))++i;if(i)s.erase(0,i);return s;}

std::string localIPv4(){
#ifdef _WIN32
    WSADATA d{};if(WSAStartup(MAKEWORD(2,2),&d)!=0)return"unavailable";char host[256]{};std::string out="unavailable";
    if(gethostname(host,sizeof(host))==0){addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;addrinfo* list=nullptr;if(getaddrinfo(host,nullptr,&hints,&list)==0){for(auto*p=list;p;p=p->ai_next){auto*a=reinterpret_cast<sockaddr_in*>(p->ai_addr);char b[INET_ADDRSTRLEN]{};if(inet_ntop(AF_INET,&a->sin_addr,b,sizeof(b))&&std::string(b)!="127.0.0.1"){out=b;break;}}freeaddrinfo(list);}}
    WSACleanup();return out;
#else
    return"unavailable";
#endif
}

std::string publicIPv4(){
#ifdef _WIN32
    HINTERNET s=WinHttpOpen(L"ClickCommand/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!s)return"unavailable";
    WinHttpSetTimeouts(s,1200,1200,1200,1600);HINTERNET c=WinHttpConnect(s,L"api.ipify.org",INTERNET_DEFAULT_HTTPS_PORT,0);if(!c){WinHttpCloseHandle(s);return"unavailable";}
    HINTERNET r=WinHttpOpenRequest(c,L"GET",L"/",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);std::string out;
    if(r&&WinHttpSendRequest(r,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(r,nullptr)){for(;;){DWORD n=0;if(!WinHttpQueryDataAvailable(r,&n)||!n)break;std::string chunk(n,'\0');DWORD got=0;if(!WinHttpReadData(r,chunk.data(),n,&got)||!got)break;chunk.resize(got);out+=chunk;if(out.size()>128)break;}}
    if(r)WinHttpCloseHandle(r);WinHttpCloseHandle(c);WinHttpCloseHandle(s);out=trim(out);return out.empty()?"unavailable":out;
#else
    return"unavailable";
#endif
}

} // namespace

class ClickCommand final : public ra::Game {
public:
    explicit ClickCommand(std::string directHost):directHost_(trim(std::move(directHost))){map_.assign(MapW*MapH,{});}

    void onStart(ra::Engine& e) override {
        e.setWindowTitle("Click Command");
        grass_=load(e,"assets/mystic/tilesets/grass.png"); plains_=load(e,"assets/mystic/plains.png");
        decor_=load(e,"assets/mystic/tilesets/decor_16x16.png"); fences_=load(e,"assets/mystic/tilesets/fences.png");
        walls_=load(e,"assets/mystic/tilesets/walls.png"); flooring_=load(e,"assets/mystic/tilesets/flooring.png");
        wooden_=load(e,"assets/mystic/tilesets/wooden.png"); chest_=load(e,"assets/mystic/objects/chest_01.png");
        player_=load(e,"assets/mystic/characters/player.png"); slime_=load(e,"assets/mystic/characters/slime.png");
        loadMap(); localIp_=localIPv4(); publicIp_=publicIPv4();
        if(!assetError_.empty()){std::ofstream out("RA-Framework-error.txt",std::ios::trunc);out<<assetError_;}
        if(!directHost_.empty())startDirectJoin(e,directHost_);
    }

    void onStop(ra::Engine&) override {socket_.close();}

    void onUpdate(ra::Engine& e,float dt) override {
        if(screen_==Screen::Splash){splash_+=dt;if(splash_>.35f||e.keyPressed(ra::Key::Enter)||e.mousePressed(ra::MouseButton::Left))screen_=Screen::Menu;return;}
        if(screen_==Screen::Menu){updateMenu(e);return;} if(screen_==Screen::Lobby){updateLobby(e,dt);return;}
        if(screen_==Screen::Editor){updateEditor(e,dt);return;} updateGame(e,dt);
    }

    void onRender(ra::Engine& e) override {
        if(screen_==Screen::Splash)renderSplash(e);else if(screen_==Screen::Menu)renderMenu(e);else if(screen_==Screen::Lobby)renderLobby(e);else if(screen_==Screen::Editor)renderEditor(e);else renderGame(e);
    }

private:
    ra::Texture load(ra::Engine&e,const char*p){try{return e.loadTexture(p);}catch(const std::exception&ex){assetError_+=std::string(p)+": "+ex.what()+"\n";return{};}}

    bool click(ra::Engine&e,float x,float y,float w,float h)const{return e.mousePressed(ra::MouseButton::Left)&&inside(e.mouseX(),e.mouseY(),x,y,x+w,y+h);}
    float menuY(const ra::Engine&e,int i)const{return e.windowHeight()*.37f+i*64.0f;}

    void updateMenu(ra::Engine&e){
        if(e.keyPressed(ra::Key::Escape)){e.quit();return;}float x=e.windowWidth()*.5f-220;
        if(e.keyPressed(ra::Key::Enter)||click(e,x,menuY(e,0),440,52)){mode_=NetMode::Offline;startGame(e);return;}
        if(click(e,x,menuY(e,1),440,52)){startHost(e);return;}
        if(click(e,x,menuY(e,2),440,52)){char* c=SDL_GetClipboardText();std::string host=c?trim(c):std::string{};if(c)SDL_free(c);if(!host.empty())startDirectJoin(e,host);else menuStatus_="COPY HOST IP FIRST";return;}
        if(click(e,x,menuY(e,3),440,52)){startJoinLan(e);return;}
        if(click(e,x,menuY(e,4),440,52)){screen_=Screen::Editor;editorCamX_=editorCamY_=0;editorZoom_=1;e.setWindowTitle("Click Command - Map Editor");return;}
    }

    void startHost(ra::Engine&e){socket_.close();mode_=NetMode::Host;connected_=false;peer_={};directTarget_={};lobbyStatus_=socket_.open(GamePort,true)?"WAITING FOR PLAYER...":"HOST ERROR: "+socket_.lastError();screen_=Screen::Lobby;e.setWindowTitle("Click Command - Host");}
    void startJoinLan(ra::Engine&e){socket_.close();mode_=NetMode::Client;connected_=false;peer_={};directTarget_={};discoverTimer_=0;lobbyStatus_=socket_.open(0,true)?"SEARCHING LAN HOST...":"JOIN ERROR: "+socket_.lastError();screen_=Screen::Lobby;e.setWindowTitle("Click Command - Join LAN");}
    void startDirectJoin(ra::Engine&e,const std::string&host){socket_.close();mode_=NetMode::Client;connected_=false;peer_={};directTarget_={trim(host),GamePort};discoverTimer_=0;lobbyStatus_=socket_.open(0,true)?"CONNECTING TO "+directTarget_.address+"...":"JOIN ERROR: "+socket_.lastError();screen_=Screen::Lobby;e.setWindowTitle("Click Command - Join Internet");}

    void updateLobby(ra::Engine&e,float dt){
        if(e.keyPressed(ra::Key::Escape)){socket_.close();mode_=NetMode::Offline;screen_=Screen::Menu;return;}
        if(mode_==NetMode::Host&&click(e,e.windowWidth()*.5f-150,e.windowHeight()*.60f,300,44)){if(publicIp_!="unavailable")SDL_SetClipboardText(publicIp_.c_str());lobbyStatus_="PUBLIC IP COPIED";}
        if(!socket_.isOpen())return;if(mode_==NetMode::Client){discoverTimer_-=dt;if(discoverTimer_<=0){discoverTimer_=.55f;ra::net::Endpoint target=directTarget_?directTarget_:ra::net::Endpoint{"255.255.255.255",GamePort};socket_.send(target,Discover,std::char_traits<char>::length(Discover));}}
        char b[1024];ra::net::Endpoint from;int n=0;while((n=socket_.receive(b,sizeof(b),from))>0){std::string m(b,b+n);if(mode_==NetMode::Host&&m==Discover){peer_=from;socket_.send(peer_,Welcome,std::char_traits<char>::length(Welcome));connected_=true;startGame(e);return;}if(mode_==NetMode::Client&&m==Welcome){peer_=from;connected_=true;startGame(e);return;}}
    }

    int localTeam()const{return mode_==NetMode::Client?1:0;}
    int unitCount(int team)const{int n=0;for(const auto&u:world_.units())if(u.team==team&&u.health>0)++n;return n;}
    int buildingCount(int team,BuildingKind k,bool active=true)const{int n=0;for(const auto&b:buildings_)if(b.team==team&&b.kind==k&&b.health>0&&(!active||b.buildLeft<=0))++n;return n;}
    bool hasBuilding(int team,BuildingKind k)const{return buildingCount(team,k)>0;}
    int unitCap(int team)const{return buildingCount(team,BuildingKind::Base)*20+buildingCount(team,BuildingKind::Silo)*17;}

    void startGame(ra::Engine&e){
        screen_=Screen::Game;gameOver_=false;winner_=-1;money_={10,10};autoClick_={0,0};world_.clear();buildings_.clear();unitKinds_.clear();busy_.clear();attackCd_.clear();nextBuildingId_=1;pendingBuild_=BuildingKind::None;zoom_=1;cameraX_=baseX_-430;cameraY_=baseY_-300;
        if(mode_!=NetMode::Client){addBuilding(BuildingKind::Base,0,baseX_,baseY_,true);spawn(UnitKind::Worker,baseX_+90,baseY_+90,0);spawn(UnitKind::Worker,baseX_-80,baseY_+100,0);spawn(UnitKind::Guerrilla,baseX_+145,baseY_+30,0);addBuilding(BuildingKind::Base,1,enemyX_,enemyY_,true);spawn(UnitKind::Worker,enemyX_+90,enemyY_+90,1);spawn(UnitKind::Worker,enemyX_-80,enemyY_+100,1);spawn(UnitKind::Guerrilla,enemyX_+145,enemyY_+30,1);if(mode_==NetMode::Offline){spawn(UnitKind::Soldier,enemyX_-120,enemyY_+20,1);spawn(UnitKind::Soldier,enemyX_+50,enemyY_-120,1);}}
        e.setWindowTitle(mode_==NetMode::Offline?"Click Command - Single Player":"Click Command - Online");
    }

    int unitCost(UnitKind k)const{switch(k){case UnitKind::Worker:return5;case UnitKind::Guerrilla:return7;case UnitKind::Soldier:return10;case UnitKind::Infantry:return16;case UnitKind::Sniper:return24;case UnitKind::Cannoneer:return30;case UnitKind::Air:return32;}return10;}
    int buildCost(BuildingKind k,int team)const{int c=999;switch(k){case BuildingKind::Barracks:c=20;break;case BuildingKind::Hangar:c=35;break;case BuildingKind::Silo:c=25;break;case BuildingKind::Crop:c=12;break;case BuildingKind::Academy:c=40;break;case BuildingKind::Training:c=30;break;default:break;}if(upgrades_[team].construction)c=(c*4)/5;return std::max(1,c);}

    void spawn(UnitKind k,float x,float y,int team){if(unitCount(team)>=unitCap(team))return;float speed=k==UnitKind::Air?150.0f:(k==UnitKind::Worker?118.0f:100.0f);auto id=world_.spawnUnit({x,y},team,speed);if(auto*u=world_.find(id)){u->maxHealth=k==UnitKind::Worker?65.0f:(k==UnitKind::Cannoneer?140.0f:100.0f);u->health=u->maxHealth;}world_.setAutonomous(id,k!=UnitKind::Worker,180);unitKinds_[id]=k;busy_[id]=0;attackCd_[id]=0;}

    void produce(int team,UnitKind k){bool ok=(k==UnitKind::Worker||k==UnitKind::Guerrilla)?hasBuilding(team,BuildingKind::Base):(k==UnitKind::Air?hasBuilding(team,BuildingKind::Hangar):hasBuilding(team,BuildingKind::Barracks));if(!ok||unitCount(team)>=unitCap(team)||money_[team]<unitCost(k))return;money_[team]-=unitCost(k);float bx=team?enemyX_:baseX_,by=team?enemyY_:baseY_;spawn(k,bx+120+(unitCount(team)%4)*20,by+110+(unitCount(team)%3)*18,team);}
    void requestUnit(UnitKind k){if(mode_==NetMode::Client)send("UNIT "+std::to_string((int)k));else produce(localTeam(),k);}

    void addBuilding(BuildingKind k,int team,float x,float y,bool complete=false){Building b;b.id=nextBuildingId_++;b.kind=k;b.team=team;b.x=x;b.y=y;b.maxHealth=k==BuildingKind::Base?700.0f:(k==BuildingKind::Silo?260.0f:360.0f);b.health=b.maxHealth;b.buildLeft=complete?0.0f:(upgrades_[team].construction?2.0f:3.5f);buildings_.push_back(b);}
    ra::rts::UnitId idleWorker(int team,bool selectedFirst=true)const{if(selectedFirst)for(const auto&u:world_.units()){auto it=unitKinds_.find(u.id);auto bt=busy_.find(u.id);if(u.team==team&&u.selected&&it!=unitKinds_.end()&&it->second==UnitKind::Worker&&bt!=busy_.end()&&bt->second<=0)return u.id;}for(const auto&u:world_.units()){auto it=unitKinds_.find(u.id);auto bt=busy_.find(u.id);if(u.team==team&&it!=unitKinds_.end()&&it->second==UnitKind::Worker&&bt!=busy_.end()&&bt->second<=0)return u.id;}return0;}
    void placeBuilding(int team,BuildingKind k,float x,float y,ra::rts::UnitId worker){auto*u=world_.find(worker);auto it=unitKinds_.find(worker);if(!u||u->team!=team||it==unitKinds_.end()||it->second!=UnitKind::Worker||busy_[worker]>0)return;int c=buildCost(k,team);if(money_[team]<c)return;money_[team]-=c;addBuilding(k,team,x,y,false);busy_[worker]=upgrades_[team].construction?2.0f:3.5f;world_.commandMoveUnits({worker},{x,y},20);pendingBuild_=BuildingKind::None;}
    void requestBuild(BuildingKind k,float x,float y,ra::rts::UnitId w){if(mode_==NetMode::Client){std::ostringstream o;o<<"BUILD "<<(int)k<<' '<<x<<' '<<y<<' '<<w;send(o.str());}else placeBuilding(localTeam(),k,x,y,w);}

    int upgradeCost(UpgradeKind k)const{switch(k){case UpgradeKind::Air:return30;case UpgradeKind::Production:return25;case UpgradeKind::Construction:return30;case UpgradeKind::LateBarracks:return40;case UpgradeKind::Barracks:return24;case UpgradeKind::Workers:return20;}return30;}
    bool hasUpgrade(int t,UpgradeKind k)const{switch(k){case UpgradeKind::Air:return upgrades_[t].air;case UpgradeKind::Production:return upgrades_[t].production;case UpgradeKind::Construction:return upgrades_[t].construction;case UpgradeKind::LateBarracks:return upgrades_[t].lateBarracks;case UpgradeKind::Barracks:return upgrades_[t].barracks;case UpgradeKind::Workers:return upgrades_[t].workers;}return false;}
    void setUpgrade(int t,UpgradeKind k){switch(k){case UpgradeKind::Air:upgrades_[t].air=true;break;case UpgradeKind::Production:upgrades_[t].production=true;break;case UpgradeKind::Construction:upgrades_[t].construction=true;break;case UpgradeKind::LateBarracks:upgrades_[t].lateBarracks=true;break;case UpgradeKind::Barracks:upgrades_[t].barracks=true;break;case UpgradeKind::Workers:upgrades_[t].workers=true;break;}}
    void research(int team,UpgradeKind k){bool acad=k==UpgradeKind::Air||k==UpgradeKind::Production||k==UpgradeKind::Construction||k==UpgradeKind::LateBarracks;if(acad&&!hasBuilding(team,BuildingKind::Academy))return;if(!acad&&!hasBuilding(team,BuildingKind::Training))return;if(hasUpgrade(team,k)||money_[team]<upgradeCost(k))return;money_[team]-=upgradeCost(k);setUpgrade(team,k);}
    void requestUpgrade(UpgradeKind k){if(mode_==NetMode::Client)send("UPGRADE "+std::to_string((int)k));else research(localTeam(),k);}

    Building* buildingAt(float x,float y,int team=-1){for(auto&b:buildings_)if(b.health>0&&(team<0||b.team==team)&&std::abs(b.x-x)<55&&std::abs(b.y-y)<55)return &b;return nullptr;}
    void harvest(int team,int id){for(auto&b:buildings_)if(b.id==id&&b.team==team&&b.kind==BuildingKind::Crop&&b.cropReady){money_[team]+=upgrades_[team].production?12:8;b.cropReady=false;b.cropTimer=0;return;}}

    float damage(UnitKind k,int team)const{float d=3;switch(k){case UnitKind::Worker:d=3;break;case UnitKind::Guerrilla:d=9;break;case UnitKind::Soldier:d=12;break;case UnitKind::Infantry:d=16;break;case UnitKind::Sniper:d=28;break;case UnitKind::Cannoneer:d=36;break;case UnitKind::Air:d=22;break;}if(k==UnitKind::Air&&upgrades_[team].air)d*=1.25f;if((k==UnitKind::Soldier||k==UnitKind::Infantry||k==UnitKind::Sniper||k==UnitKind::Cannoneer)&&upgrades_[team].barracks)d*=1.2f;if((k==UnitKind::Sniper||k==UnitKind::Cannoneer)&&upgrades_[team].lateBarracks)d*=1.25f;return d;}
    float range(UnitKind k)const{switch(k){case UnitKind::Worker:return42;case UnitKind::Guerrilla:return95;case UnitKind::Soldier:return105;case UnitKind::Infantry:return115;case UnitKind::Sniper:return245;case UnitKind::Cannoneer:return175;case UnitKind::Air:return165;}return100;}

    void updateEconomy(float dt){for(auto&kv:busy_)if(kv.second>0)kv.second=std::max(0.0f,kv.second-dt);for(auto&b:buildings_){if(b.health<=0)continue;if(b.buildLeft>0){b.buildLeft=std::max(0.0f,b.buildLeft-dt);continue;}if(b.kind==BuildingKind::Crop&&!b.cropReady){b.cropTimer+=dt;if(b.cropTimer>=7)b.cropReady=true;}}for(int t=0;t<2;++t){int idle=0;for(const auto&u:world_.units()){auto it=unitKinds_.find(u.id);auto bt=busy_.find(u.id);if(u.team==t&&u.health>0&&it!=unitKinds_.end()&&it->second==UnitKind::Worker&&bt!=busy_.end()&&bt->second<=0)++idle;}autoClick_[t]+=idle*(upgrades_[t].workers?1.5f:1.0f)*dt;int add=(int)std::floor(autoClick_[t]);if(add){money_[t]+=add;autoClick_[t]-=add;}}}

    void updateCombat(float dt){for(auto&kv:attackCd_)kv.second=std::max(0.0f,kv.second-dt);for(auto&u:world_.units()){if(u.health<=0||attackCd_[u.id]>0)continue;UnitKind k=unitKinds_.count(u.id)?unitKinds_.at(u.id):UnitKind::Soldier;float best=range(k)*range(k);ra::rts::Unit*target=nullptr;for(auto&v:world_.units())if(v.team!=u.team&&v.health>0){float dx=v.position.x-u.position.x,dy=v.position.y-u.position.y,d=dx*dx+dy*dy;if(d<best){best=d;target=&v;}}Building*bt=nullptr;if(!target)for(auto&b:buildings_)if(b.team!=u.team&&b.health>0&&b.buildLeft<=0){float dx=b.x-u.position.x,dy=b.y-u.position.y,d=dx*dx+dy*dy;if(d<best){best=d;bt=&b;}}if(target||bt){float d=damage(k,u.team);if(target)target->health-=d;else bt->health-=d;attackCd_[u.id]=k==UnitKind::Sniper?1.6f:(k==UnitKind::Cannoneer?1.3f:.85f);}}
        std::vector<ra::rts::Unit>alive;for(auto&u:world_.units())if(u.health>0)alive.push_back(u);if(alive.size()!=world_.units().size())world_.replaceUnits(alive);buildings_.erase(std::remove_if(buildings_.begin(),buildings_.end(),[](const Building&b){return b.health<=0;}),buildings_.end());}

    void checkDefeat(){if(gameOver_)return;for(int t=0;t<2;++t)if(buildingCount(t,BuildingKind::Base,false)==0&&unitCount(t)==0){gameOver_=true;winner_=1-t;}}

    void updateGame(ra::Engine&e,float dt){
        if(e.keyPressed(ra::Key::Escape)){socket_.close();mode_=NetMode::Offline;screen_=Screen::Menu;return;}pumpNetwork();float panel=hudX(e),mx=e.mouseX(),my=e.mouseY();if(e.mouseWheelY()!=0)zoom_=clampf(zoom_+e.mouseWheelY()*.12f,.65f,2.5f);float dx=0,dy=0;if(e.keyDown(ra::Key::A)||e.keyDown(ra::Key::Left))dx--;if(e.keyDown(ra::Key::D)||e.keyDown(ra::Key::Right))dx++;if(e.keyDown(ra::Key::W)||e.keyDown(ra::Key::Up))dy--;if(e.keyDown(ra::Key::S)||e.keyDown(ra::Key::Down))dy++;cameraX_+=dx*560*dt/zoom_;cameraY_+=dy*560*dt/zoom_;clampCamera(e);if(!gameOver_)handleMouse(e,panel,mx,my);if(mode_!=NetMode::Client&&!gameOver_){world_.update(dt);updateEconomy(dt);updateCombat(dt);checkDefeat();if(mode_==NetMode::Host&&connected_){snapshotTimer_-=dt;if(snapshotTimer_<=0){snapshotTimer_=.08f;sendSnapshot();}}}for(auto&f:effects_){f.life-=dt;f.y-=24*dt;}effects_.erase(std::remove_if(effects_.begin(),effects_.end(),[](const ClickFx&f){return f.life<=0;}),effects_.end());}

    void handleMouse(ra::Engine&e,float panel,float mx,float my){int t=localTeam();if(e.mousePressed(ra::MouseButton::Left)&&mx>=panel){if(handleHud(e,mx,my))return;}if(e.mousePressed(ra::MouseButton::Left)&&mx<panel){float wx=wX(mx),wy=wY(my);if(pendingBuild_!=BuildingKind::None){auto w=idleWorker(t);if(w)requestBuild(pendingBuild_,wx,wy,w);return;}if(Building*b=buildingAt(wx,wy,t);b&&b->kind==BuildingKind::Crop&&b->cropReady){if(mode_==NetMode::Client)send("HARVEST "+std::to_string(b->id));else harvest(t,b->id);return;}drag_=true;sx_=ex_=mx;sy_=ey_=my;}if(drag_&&e.mouseDown(ra::MouseButton::Left)){ex_=mx;ey_=my;}if(drag_&&e.mouseReleased(ra::MouseButton::Left)){ex_=mx;ey_=my;float ax=ex_-sx_,ay=ey_-sy_;if(ax*ax+ay*ay<36){if(mode_==NetMode::Client)send("CLICK");else ++money_[t];effects_.push_back({mx,my,.7f});world_.selectPoint({wX(mx),wY(my)},26.0f/zoom_,false);}else world_.selectRect({wX(std::min(sx_,ex_)),wY(std::min(sy_,ey_)),wX(std::max(sx_,ex_)),wY(std::max(sy_,ey_))},false);for(auto&u:world_.units())if(u.selected&&u.team!=t)u.selected=false;drag_=false;}if(e.mousePressed(ra::MouseButton::Right)&&mx<panel){pendingBuild_=BuildingKind::None;float x=wX(mx),y=wY(my);if(mode_==NetMode::Client)sendMove(x,y);else world_.commandMove({x,y},34);}}

    bool hudHit(ra::Engine&e,float mx,float my,int row)const{float x=hudX(e)+12,y=138+row*38,w=e.windowWidth()-hudX(e)-24;return inside(mx,my,x,y,x+w,y+32);}
    bool handleHud(ra::Engine&e,float mx,float my){int t=localTeam(),r=0;auto hit=[&](bool en=true){bool h=en&&hudHit(e,mx,my,r);++r;return h;};if(hit(hasBuilding(t,BuildingKind::Base))){requestUnit(UnitKind::Worker);return true;}if(hit(hasBuilding(t,BuildingKind::Base))){requestUnit(UnitKind::Guerrilla);return true;}if(hit(idleWorker(t,false)!=0)){pendingBuild_=BuildingKind::Barracks;return true;}if(hit(idleWorker(t,false)!=0)){pendingBuild_=BuildingKind::Hangar;return true;}if(hit(idleWorker(t,false)!=0)){pendingBuild_=BuildingKind::Silo;return true;}if(hit(idleWorker(t,false)!=0)){pendingBuild_=BuildingKind::Crop;return true;}if(hit(idleWorker(t,false)!=0)){pendingBuild_=BuildingKind::Academy;return true;}if(hit(idleWorker(t,false)!=0)){pendingBuild_=BuildingKind::Training;return true;}if(hit(hasBuilding(t,BuildingKind::Barracks))){requestUnit(UnitKind::Soldier);return true;}if(hit(hasBuilding(t,BuildingKind::Barracks))){requestUnit(UnitKind::Infantry);return true;}if(hit(hasBuilding(t,BuildingKind::Barracks))){requestUnit(UnitKind::Sniper);return true;}if(hit(hasBuilding(t,BuildingKind::Barracks))){requestUnit(UnitKind::Cannoneer);return true;}if(hit(hasBuilding(t,BuildingKind::Hangar))){requestUnit(UnitKind::Air);return true;}if(hit(hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].air)){requestUpgrade(UpgradeKind::Air);return true;}if(hit(hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].production)){requestUpgrade(UpgradeKind::Production);return true;}if(hit(hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].construction)){requestUpgrade(UpgradeKind::Construction);return true;}if(hit(hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].lateBarracks)){requestUpgrade(UpgradeKind::LateBarracks);return true;}if(hit(hasBuilding(t,BuildingKind::Training)&&!upgrades_[t].barracks)){requestUpgrade(UpgradeKind::Barracks);return true;}if(hit(hasBuilding(t,BuildingKind::Training)&&!upgrades_[t].workers)){requestUpgrade(UpgradeKind::Workers);return true;}return false;}

    void send(const std::string&s){if(connected_&&peer_&&socket_.isOpen())socket_.send(peer_,s.data(),s.size());}
    void sendMove(float x,float y){std::ostringstream o;o<<"MOVE "<<std::fixed<<std::setprecision(2)<<x<<' '<<y;for(auto&u:world_.units())if(u.selected&&u.team==1)o<<' '<<u.id;send(o.str());}
    bool samePeer(const ra::net::Endpoint&f)const{return peer_&&f.address==peer_.address&&f.port==peer_.port;}
    void pumpNetwork(){if(mode_==NetMode::Offline||!socket_.isOpen())return;char b[65507];ra::net::Endpoint from;int n=0;while((n=socket_.receive(b,sizeof(b),from))>0){std::string m(b,b+n);if(mode_==NetMode::Host)hostPacket(m,from);else clientPacket(m,from);}}
    void hostPacket(const std::string&m,const ra::net::Endpoint&f){if(!samePeer(f))return;if(m=="CLICK"){++money_[1];return;}if(m.rfind("UNIT ",0)==0){int k=std::atoi(m.c_str()+5);if(k>=0&&k<=6)produce(1,(UnitKind)k);return;}if(m.rfind("BUILD ",0)==0){std::istringstream in(m.substr(6));int k;float x,y;ra::rts::UnitId w;if(in>>k>>x>>y>>w&&k>=1&&k<=6)placeBuilding(1,(BuildingKind)k,x,y,w);return;}if(m.rfind("HARVEST ",0)==0){harvest(1,std::atoi(m.c_str()+8));return;}if(m.rfind("UPGRADE ",0)==0){int k=std::atoi(m.c_str()+8);if(k>=0&&k<=5)research(1,(UpgradeKind)k);return;}if(m.rfind("MOVE ",0)==0){std::istringstream in(m.substr(5));float x,y;in>>x>>y;std::vector<ra::rts::UnitId>ids;ra::rts::UnitId id;while(in>>id){auto*u=world_.find(id);if(u&&u->team==1)ids.push_back(id);}world_.commandMoveUnits(ids,{x,y},34);}}

    int upgradeBits(int t)const{return(upgrades_[t].air?1:0)|(upgrades_[t].production?2:0)|(upgrades_[t].construction?4:0)|(upgrades_[t].lateBarracks?8:0)|(upgrades_[t].barracks?16:0)|(upgrades_[t].workers?32:0);}
    void setBits(int t,int b){upgrades_[t].air=b&1;upgrades_[t].production=b&2;upgrades_[t].construction=b&4;upgrades_[t].lateBarracks=b&8;upgrades_[t].barracks=b&16;upgrades_[t].workers=b&32;}
    void sendSnapshot(){std::ostringstream o;o<<"S "<<money_[0]<<' '<<money_[1]<<' '<<(gameOver_?1:0)<<' '<<winner_<<' '<<upgradeBits(0)<<' '<<upgradeBits(1)<<' '<<world_.units().size()<<' '<<buildings_.size()<<std::fixed<<std::setprecision(2);for(auto&u:world_.units()){int k=unitKinds_.count(u.id)?(int)unitKinds_[u.id]:2;float b=busy_.count(u.id)?busy_[u.id]:0;o<<' '<<u.id<<' '<<u.position.x<<' '<<u.position.y<<' '<<u.destination.x<<' '<<u.destination.y<<' '<<u.home.x<<' '<<u.home.y<<' '<<u.speed<<' '<<u.team<<' '<<(u.autonomous?1:0)<<' '<<(int)u.order<<' '<<u.aiStep<<' '<<u.thinkCooldown<<' '<<u.patrolRadius<<' '<<u.health<<' '<<u.maxHealth<<' '<<k<<' '<<b;}for(auto&b:buildings_)o<<' '<<b.id<<' '<<(int)b.kind<<' '<<b.team<<' '<<b.x<<' '<<b.y<<' '<<b.health<<' '<<b.maxHealth<<' '<<b.buildLeft<<' '<<b.cropTimer<<' '<<(b.cropReady?1:0);send(o.str());}
    void clientPacket(const std::string&m,const ra::net::Endpoint&f){if(!samePeer(f)||m.rfind("S ",0)!=0)return;std::vector<ra::rts::UnitId>sel;for(auto&u:world_.units())if(u.selected)sel.push_back(u.id);std::istringstream in(m.substr(2));int over,b0,b1,uc,bc;if(!(in>>money_[0]>>money_[1]>>over>>winner_>>b0>>b1>>uc>>bc)||uc<0||uc>2048||bc<0||bc>1024)return;gameOver_=over!=0;setBits(0,b0);setBits(1,b1);std::vector<ra::rts::Unit>units;std::unordered_map<ra::rts::UnitId,UnitKind>kinds;std::unordered_map<ra::rts::UnitId,float>busy;for(int i=0;i<uc;++i){ra::rts::Unit u;int au,ord,k;float wb;if(!(in>>u.id>>u.position.x>>u.position.y>>u.destination.x>>u.destination.y>>u.home.x>>u.home.y>>u.speed>>u.team>>au>>ord>>u.aiStep>>u.thinkCooldown>>u.patrolRadius>>u.health>>u.maxHealth>>k>>wb))return;u.autonomous=au!=0;u.order=(ra::rts::UnitOrder)std::max(0,std::min(ord,2));u.radius=12;u.selected=u.team==1&&std::find(sel.begin(),sel.end(),u.id)!=sel.end();units.push_back(u);kinds[u.id]=(UnitKind)std::max(0,std::min(k,6));busy[u.id]=wb;}std::vector<Building>bs;for(int i=0;i<bc;++i){Building b;int k,ready;if(!(in>>b.id>>k>>b.team>>b.x>>b.y>>b.health>>b.maxHealth>>b.buildLeft>>b.cropTimer>>ready))return;b.kind=(BuildingKind)std::max(0,std::min(k,6));b.cropReady=ready!=0;bs.push_back(b);nextBuildingId_=std::max(nextBuildingId_,b.id+1);}world_.replaceUnits(units);unitKinds_=std::move(kinds);busy_=std::move(busy);buildings_=std::move(bs);}

    const ra::Texture& sheet(Sheet s)const{switch(s){case Sheet::Grass:return grass_;case Sheet::Plains:return plains_;case Sheet::Decor:return decor_;case Sheet::Fences:return fences_;case Sheet::Walls:return walls_;case Sheet::Flooring:return flooring_;case Sheet::Wooden:return wooden_;case Sheet::Chest:return chest_;}return grass_;}
    const std::vector<PaletteItem>& palette()const{static const std::vector<PaletteItem>p={{Sheet::Grass,0,0,"GRASS"},{Sheet::Plains,4,4,"PLAIN"},{Sheet::Plains,4,0,"DIRT"},{Sheet::Plains,1,4,"PATH"},{Sheet::Decor,0,0,"DECOR"},{Sheet::Decor,1,0,"DECOR"},{Sheet::Fences,0,1,"FENCE"},{Sheet::Fences,1,1,"FENCE"},{Sheet::Walls,0,1,"WALL"},{Sheet::Walls,1,0,"WALL"},{Sheet::Flooring,0,0,"FLOOR"},{Sheet::Wooden,0,0,"WOOD"},{Sheet::Chest,0,0,"CHEST"}};return p;}
    void drawMapCell(ra::Engine&e,int tx,int ty,float dx,float dy,float s)const{e.drawTexture(grass_,{dx,dy,s,s});const auto&c=map_[ty*MapW+tx];if(c.sheet>=0){Sheet sh=(Sheet)c.sheet;if(sh==Sheet::Grass)e.drawTexture(grass_,{dx,dy,s,s});else e.drawTextureRegion(sheet(sh),tile16(c.sx,c.sy),{dx,dy,s,s});}}
    void updateEditor(ra::Engine&e,float dt){if(e.keyPressed(ra::Key::Escape)){screen_=Screen::Menu;e.setWindowTitle("Click Command");return;}float panel=280,mx=e.mouseX(),my=e.mouseY();if(e.mouseWheelY()!=0)editorZoom_=clampf(editorZoom_+e.mouseWheelY()*.15f,.5f,3.0f);float dx=0,dy=0;if(e.keyDown(ra::Key::A)||e.keyDown(ra::Key::Left))dx--;if(e.keyDown(ra::Key::D)||e.keyDown(ra::Key::Right))dx++;if(e.keyDown(ra::Key::W)||e.keyDown(ra::Key::Up))dy--;if(e.keyDown(ra::Key::S)||e.keyDown(ra::Key::Down))dy++;editorCamX_=clampf(editorCamX_+dx*500*dt/editorZoom_,0,std::max(0.0f,MapW*Tile-(e.windowWidth()-panel)/editorZoom_));editorCamY_=clampf(editorCamY_+dy*500*dt/editorZoom_,0,std::max(0.0f,MapH*Tile-e.windowHeight()/editorZoom_));if(e.mousePressed(ra::MouseButton::Left)&&mx>e.windowWidth()-panel){float px=e.windowWidth()-panel+14,py=54;int col=(int)((mx-px)/58),row=(int)((my-py)/58),idx=row*4+col;if(col>=0&&col<4&&idx>=0&&idx<(int)palette().size()){editorSelected_=idx;return;}if(click(e,e.windowWidth()-panel+14,e.windowHeight()-150,112,32)){saveMap();return;}if(click(e,e.windowWidth()-panel+140,e.windowHeight()-150,126,32)){loadMap();return;}if(click(e,e.windowWidth()-panel+14,e.windowHeight()-106,panel-28,32)){std::fill(map_.begin(),map_.end(),MapCell{});return;}if(click(e,e.windowWidth()-panel+14,e.windowHeight()-62,panel-28,32)){screen_=Screen::Menu;return;}}if(mx<e.windowWidth()-panel&&(e.mouseDown(ra::MouseButton::Left)||e.mouseDown(ra::MouseButton::Right))){int tx=(int)((editorCamX_+mx/editorZoom_)/Tile),ty=(int)((editorCamY_+my/editorZoom_)/Tile);if(tx>=0&&tx<MapW&&ty>=0&&ty<MapH){if(e.mouseDown(ra::MouseButton::Right))map_[ty*MapW+tx]={};else{auto&p=palette()[editorSelected_];map_[ty*MapW+tx]={(int)p.sheet,p.sx,p.sy};}}}}
    void saveMap(){std::filesystem::create_directories("maps");std::ofstream out("maps/editor_map.txt",std::ios::trunc);if(!out)return;out<<"CCMAP 2 "<<MapW<<' '<<MapH<<'\n';for(int y=0;y<MapH;++y)for(int x=0;x<MapW;++x){auto&c=map_[y*MapW+x];if(c.sheet>=0)out<<x<<' '<<y<<' '<<c.sheet<<' '<<c.sx<<' '<<c.sy<<'\n';}}
    void loadMap(){std::fill(map_.begin(),map_.end(),MapCell{});std::ifstream in("maps/editor_map.txt");if(!in)return;std::string tag;int ver,w,h;if(!(in>>tag>>ver>>w>>h)||tag!="CCMAP")return;int x,y,s,sx,sy;while(in>>x>>y>>s>>sx>>sy)if(x>=0&&x<MapW&&y>=0&&y<MapH&&s>=0&&s<=7)map_[y*MapW+x]={s,sx,sy};}

    void renderSplash(ra::Engine&e){float w=e.windowWidth(),h=e.windowHeight();e.clear({8,13,9,255});for(int y=0;y<12;++y)for(int x=0;x<22;++x)e.drawTexture(grass_,{w*.5f-352+x*32,h*.04f+y*32,32,32});e.drawTextureRegion(player_,frame48(0,0),{w*.5f-48,h*.28f,96,96});e.drawText(w*.5f-60,h*.47f,"CLICK COMMAND",{245,238,205,255});}
    void menuButton(ra::Engine&e,float x,float y,const char*s){bool hot=inside(e.mouseX(),e.mouseY(),x,y,x+440,y+52);e.fillRect({x,y,440,52},hot?ra::Color{53,104,52,245}:ra::Color{29,63,34,245});e.drawRect({x,y,440,52},{125,167,121,255});e.drawText(x+100,y+21,s,{247,244,220,255});}
    void renderMenu(ra::Engine&e){float w=e.windowWidth(),h=e.windowHeight();e.clear({8,12,9,255});for(int y=0;y<18;++y)for(int x=0;x<30;++x)e.drawTexture(grass_,{w*.5f-480+x*32,h*.01f+y*32,32,32});e.fillRect({w*.5f-300,h*.12f,600,700},{10,16,12,225});e.drawText(w*.5f-60,h*.18f,"CLICK COMMAND",{245,238,205,255});e.drawText(w*.5f-96,h*.22f,"PUBLIC IP: "+publicIp_,{255,220,90,255});e.drawText(w*.5f-96,h*.25f,"LOCAL IP:  "+localIp_,{190,220,190,255});float x=w*.5f-220;menuButton(e,x,menuY(e,0),"SINGLE PLAYER");menuButton(e,x,menuY(e,1),"HOST INTERNET GAME");menuButton(e,x,menuY(e,2),"JOIN INTERNET FROM CLIPBOARD");menuButton(e,x,menuY(e,3),"JOIN LAN GAME");menuButton(e,x,menuY(e,4),"MAP EDITOR");if(!menuStatus_.empty())e.drawText(w*.5f-120,h*.72f,menuStatus_,{255,150,100,255});}
    void renderLobby(ra::Engine&e){float w=e.windowWidth(),h=e.windowHeight();e.clear({9,14,10,255});e.fillRect({w*.5f-350,h*.24f,700,420},{14,22,16,255});e.drawRect({w*.5f-350,h*.24f,700,420},{125,167,121,255});e.drawText(w*.5f-55,h*.30f,mode_==NetMode::Host?"HOST GAME":"JOIN GAME",{245,238,205,255});e.drawText(w*.5f-160,h*.38f,lobbyStatus_,{190,220,190,255});if(mode_==NetMode::Host){e.drawText(w*.5f-155,h*.46f,"PUBLIC IP: "+publicIp_,{255,220,90,255});e.drawText(w*.5f-155,h*.50f,"PORT: 46000 UDP",{190,220,190,255});e.fillRect({w*.5f-150,h*.60f,300,44},{38,76,42,255});e.drawText(w*.5f-70,h*.62f,"COPY PUBLIC IP",{245,238,205,255});}}

    void drawTerrain(ra::Engine&e,float vw,float vh){int fx=std::max(0,(int)std::floor(cameraX_/Tile)-1),fy=std::max(0,(int)std::floor(cameraY_/Tile)-1),lx=std::min(MapW-1,fx+(int)(vw/(Tile*zoom_))+4),ly=std::min(MapH-1,fy+(int)(vh/(Tile*zoom_))+4);for(int y=fy;y<=ly;++y)for(int x=fx;x<=lx;++x){float s=Tile*zoom_,dx=(x*Tile-cameraX_)*zoom_,dy=(y*Tile-cameraY_)*zoom_;drawMapCell(e,x,y,dx,dy,s);}}
    void worldTile(ra::Engine&e,const ra::Texture&t,ra::Rect src,float wx,float wy){float s=Tile*zoom_;e.drawTextureRegion(t,src,{(wx-cameraX_)*zoom_,(wy-cameraY_)*zoom_,s,s});}
    void drawBuilding(ra::Engine&e,const Building&b){float x=b.x,y=b.y,z=Tile;if(b.kind==BuildingKind::Base){for(int yy=-2;yy<=2;++yy)for(int xx=-3;xx<=3;++xx)worldTile(e,wooden_,tile16(0,0),x+xx*z,y+yy*z);for(int xx=-3;xx<=3;++xx){worldTile(e,walls_,tile16(1,0),x+xx*z,y-2*z);worldTile(e,walls_,tile16(1,3),x+xx*z,y+2*z);}}else if(b.kind==BuildingKind::Barracks){for(int yy=-1;yy<=1;++yy)for(int xx=-2;xx<=2;++xx)worldTile(e,walls_,tile16(1+(xx==2),1+(yy==1)),x+xx*z,y+yy*z);}else if(b.kind==BuildingKind::Hangar){for(int yy=-2;yy<=2;++yy)for(int xx=-2;xx<=2;++xx)worldTile(e,flooring_,tile16((xx+2)%3,(yy+2)%3),x+xx*z,y+yy*z);}else if(b.kind==BuildingKind::Silo){for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx)worldTile(e,chest_,tile16(0,0),x+xx*z,y+yy*z);}else if(b.kind==BuildingKind::Crop){for(int yy=-1;yy<=1;++yy)for(int xx=-2;xx<=2;++xx){worldTile(e,plains_,tile16(4,0),x+xx*z,y+yy*z);if(b.cropReady)worldTile(e,decor_,tile16((xx+2)%4,0),x+xx*z,y+yy*z);}}else if(b.kind==BuildingKind::Academy){for(int yy=-2;yy<=1;++yy)for(int xx=-2;xx<=2;++xx)worldTile(e,walls_,tile16((xx+2)%3,(yy+2)%3),x+xx*z,y+yy*z);}else if(b.kind==BuildingKind::Training){for(int xx=-3;xx<=3;++xx){worldTile(e,fences_,tile16(1,1),x+xx*z,y-2*z);worldTile(e,fences_,tile16(1,1),x+xx*z,y+2*z);}for(int yy=-1;yy<=1;++yy){worldTile(e,fences_,tile16(0,1),x-3*z,y+yy*z);worldTile(e,fences_,tile16(0,1),x+3*z,y+yy*z);}}float sx=(x-cameraX_)*zoom_,sy=(y-cameraY_)*zoom_;std::string name=buildingName(b.kind);if(b.buildLeft>0)name+=" BUILDING";if(b.kind==BuildingKind::Crop&&b.cropReady)name+=" READY";e.drawText(sx-42,sy-82*zoom_,name,b.team==0?ra::Color{220,240,255,255}:ra::Color{190,230,190,255});}
    void drawUnits(ra::Engine&e,float vw,float vh){int i=0;for(const auto&u:world_.units()){float x=(u.position.x-cameraX_)*zoom_,y=(u.position.y-cameraY_)*zoom_;UnitKind k=unitKinds_.count(u.id)?unitKinds_.at(u.id):UnitKind::Soldier;float s=(k==UnitKind::Air?58.0f:(k==UnitKind::Worker?46.0f:52.0f))*zoom_;if(x<-s||y<-s||x>vw+s||y>vh+s){++i;continue;}if(u.selected)e.drawRect({x-s*.55f,y-s*.55f,s*1.1f,s*1.1f},{96,238,104,255});if(u.team==0)e.drawTextureRegion(player_,frame48((i+(int)u.aiStep)%6,0),{x-s*.5f,y-s*.7f,s,s});else e.drawTextureRegion(slime_,frame32((i+(int)u.aiStep)%4,0),{x-s*.5f,y-s*.5f,s,s});++i;}}
    void hudButton(ra::Engine&e,float x,float y,float w,const std::string&s,bool en){e.fillRect({x,y,w,32},en?ra::Color{38,78,42,255}:ra::Color{31,36,32,255});e.drawRect({x,y,w,32},{91,118,95,255});e.drawText(x+7,y+12,s,en?ra::Color{240,239,214,255}:ra::Color{110,118,111,255});}
    void drawHud(ra::Engine&e,float x,float h){int t=localTeam();float w=e.windowWidth()-x;e.fillRect({x,0,w,h},{12,18,14,255});e.drawText(x+12,16,"CLICK COMMAND",{245,238,203,255});e.drawText(x+12,38,"MONEY: "+std::to_string(money_[t]),{255,216,64,255});e.drawText(x+12,58,"UNITS: "+std::to_string(unitCount(t))+" / "+std::to_string(unitCap(t)),{190,205,193,255});e.drawText(x+12,78,"IDLE WORKER = +1 CLICK/s",{145,170,150,255});e.drawText(x+12,98,"GUERRILLA = 75% SOLDIER DAMAGE",{145,170,150,255});float bx=x+12,bw=w-24;int r=0;auto add=[&](const std::string&s,bool en=true){hudButton(e,bx,138+r*38,bw,s,en);++r;};add("WORKER $5",hasBuilding(t,BuildingKind::Base)&&unitCount(t)<unitCap(t));add("GUERRILLA $7",hasBuilding(t,BuildingKind::Base)&&unitCount(t)<unitCap(t));add("BUILD BARRACKS $"+std::to_string(buildCost(BuildingKind::Barracks,t)),idleWorker(t,false)!=0);add("BUILD HANGAR $"+std::to_string(buildCost(BuildingKind::Hangar,t)),idleWorker(t,false)!=0);add("BUILD SILO $"+std::to_string(buildCost(BuildingKind::Silo,t))+"  +17 CAP",idleWorker(t,false)!=0);add("BUILD CROP $"+std::to_string(buildCost(BuildingKind::Crop,t))+"  7s",idleWorker(t,false)!=0);add("BUILD ACADEMY $"+std::to_string(buildCost(BuildingKind::Academy,t)),idleWorker(t,false)!=0);add("BUILD TRAINING $"+std::to_string(buildCost(BuildingKind::Training,t)),idleWorker(t,false)!=0);add("SOLDIER $10",hasBuilding(t,BuildingKind::Barracks));add("INFANTRY $16",hasBuilding(t,BuildingKind::Barracks));add("SNIPER $24",hasBuilding(t,BuildingKind::Barracks));add("CANNONEER $30",hasBuilding(t,BuildingKind::Barracks));add("AIR UNIT $32",hasBuilding(t,BuildingKind::Hangar));add("ACADEMY: AIR $30",hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].air);add("ACADEMY: PRODUCTION $25",hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].production);add("ACADEMY: CONSTRUCTION $30",hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].construction);add("ACADEMY: LATEGAME $40",hasBuilding(t,BuildingKind::Academy)&&!upgrades_[t].lateBarracks);add("TRAINING: BARRACKS $24",hasBuilding(t,BuildingKind::Training)&&!upgrades_[t].barracks);add("TRAINING: WORKERS $20",hasBuilding(t,BuildingKind::Training)&&!upgrades_[t].workers);}
    void renderGame(ra::Engine&e){float vw=hudX(e),vh=e.windowHeight();e.clear({8,13,9,255});drawTerrain(e,vw,vh);for(const auto&b:buildings_)drawBuilding(e,b);drawUnits(e,vw,vh);if(drag_){float l=std::min(sx_,ex_),r=std::max(sx_,ex_),t=std::min(sy_,ey_),b=std::max(sy_,ey_);e.drawRect({l,t,r-l,b-t},{96,238,104,255});}if(pendingBuild_!=BuildingKind::None)e.drawText(16,16,std::string("PLACE ")+buildingName(pendingBuild_)+" - RMB CANCEL",{255,220,90,255});for(auto&f:effects_)e.drawText(f.x-4,f.y,"+1",{255,220,70,255});drawHud(e,vw,vh);if(gameOver_){e.fillRect({vw*.5f-180,vh*.45f,360,90},{12,12,12,220});e.drawText(vw*.5f-30,vh*.49f,winner_==localTeam()?"VICTORY":"DEFEAT",{255,220,90,255});}}
    void renderEditor(ra::Engine&e){float panel=280,w=e.windowWidth(),h=e.windowHeight();e.clear({8,13,9,255});int fx=std::max(0,(int)(editorCamX_/Tile)-1),fy=std::max(0,(int)(editorCamY_/Tile)-1),lx=std::min(MapW-1,fx+(int)((w-panel)/(Tile*editorZoom_))+3),ly=std::min(MapH-1,fy+(int)(h/(Tile*editorZoom_))+3);for(int y=fy;y<=ly;++y)for(int x=fx;x<=lx;++x){float s=Tile*editorZoom_,dx=(x*Tile-editorCamX_)*editorZoom_,dy=(y*Tile-editorCamY_)*editorZoom_;drawMapCell(e,x,y,dx,dy,s);e.drawRect({dx,dy,s,s},{28,45,30,80});}e.fillRect({w-panel,0,panel,h},{12,18,14,255});e.drawText(w-panel+14,18,"MAP EDITOR",{245,238,205,255});e.drawText(w-panel+14,36,"LMB PAINT / RMB ERASE",{160,185,165,255});float px=w-panel+14,py=54;for(size_t i=0;i<palette().size();++i){int c=(int)i%4,r=(int)i/4;float x=px+c*58,y=py+r*58;auto&p=palette()[i];if(p.sheet==Sheet::Grass)e.drawTexture(grass_,{x,y,48,48});else e.drawTextureRegion(sheet(p.sheet),tile16(p.sx,p.sy),{x,y,48,48});if((int)i==editorSelected_)e.drawRect({x-2,y-2,52,52},{255,220,70,255});}auto eb=[&](float x,float y,float ww,const char*s){e.fillRect({x,y,ww,32},{37,75,41,255});e.drawRect({x,y,ww,32},{105,140,110,255});e.drawText(x+10,y+12,s,{240,239,214,255});};eb(w-panel+14,h-150,112,"SAVE");eb(w-panel+140,h-150,126,"LOAD");eb(w-panel+14,h-106,panel-28,"CLEAR MAP");eb(w-panel+14,h-62,panel-28,"BACK TO MENU");}

    float hudX(const ra::Engine&e)const{return std::max(760.0f,(float)e.windowWidth()-330.0f);}float wX(float x)const{return cameraX_+x/zoom_;}float wY(float y)const{return cameraY_+y/zoom_;}
    void clampCamera(const ra::Engine&e){float vw=hudX(e)/zoom_,vh=e.windowHeight()/zoom_;cameraX_=clampf(cameraX_,0,std::max(0.0f,MapW*Tile-vw));cameraY_=clampf(cameraY_,0,std::max(0.0f,MapH*Tile-vh));}

    Screen screen_{Screen::Splash};NetMode mode_{NetMode::Offline};float splash_{};ra::rts::World world_{{0,0,MapW*Tile,MapH*Tile}};std::unordered_map<ra::rts::UnitId,UnitKind>unitKinds_;std::unordered_map<ra::rts::UnitId,float>busy_,attackCd_;std::vector<Building>buildings_;std::array<Upgrades,2>upgrades_{};std::array<int,2>money_{{10,10}};std::array<float,2>autoClick_{{0,0}};int nextBuildingId_{1};bool gameOver_{};int winner_{-1};BuildingKind pendingBuild_{BuildingKind::None};std::vector<ClickFx>effects_;float cameraX_{},cameraY_{},zoom_{1};bool drag_{};float sx_{},sy_{},ex_{},ey_{};const float baseX_=26*Tile,baseY_=20*Tile,enemyX_=92*Tile,enemyY_=56*Tile;
    ra::Texture grass_,plains_,decor_,fences_,walls_,flooring_,wooden_,chest_,player_,slime_;std::string assetError_,localIp_="unavailable",publicIp_="unavailable",menuStatus_,directHost_;ra::net::DatagramSocket socket_;ra::net::Endpoint peer_{},directTarget_{};bool connected_{};float discoverTimer_{},snapshotTimer_{};std::string lobbyStatus_;std::vector<MapCell>map_;float editorCamX_{},editorCamY_{},editorZoom_{1};int editorSelected_{};
};

int main(){
    ra::EngineConfig c;c.title="Click Command";c.width=1440;c.height=900;c.resizable=true;c.vsync=true;
    std::string direct;if(const char* p=std::getenv("CC_HOST"))direct=p;
    try{ra::Engine e(c);ClickCommand game(direct);return e.run(game);}catch(const std::exception&ex){std::ofstream out("RA-Framework-error.txt",std::ios::trunc);out<<"Fatal startup/runtime error:\n"<<ex.what()<<'\n';std::fprintf(stderr,"Fatal error: %s\n",ex.what());
#ifdef _WIN32
        MessageBoxA(nullptr,ex.what(),"Click Command - Fatal Error",MB_OK|MB_ICONERROR);
#endif
        return 1;}
}
