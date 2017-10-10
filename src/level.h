#ifndef H_LEVEL
#define H_LEVEL

#include "core.h"
#include "utils.h"
#include "format.h"
#include "cache.h"
#include "lara.h"
#include "enemy.h"
#include "camera.h"
#include "trigger.h"
#include "inventory.h"

#ifdef _DEBUG
    #include "debug.h"
#endif

extern ShaderCache *shaderCache;
extern void loadAsync(Stream *stream, void *userData);

struct Level : IGame {
    TR::Level   level;
    Inventory   inventory;
    Texture     *atlas;
    Texture     *cube;
    MeshBuilder *mesh;

    Lara        *lara;
    Camera      *camera;
    Texture     *shadow;

    struct Params {
        float   time;
        float   waterHeight;
        float   clipSign;
        float   clipHeight;
    } *params = (Params*)&Core::params;

    ZoneCache    *zoneCache;
    AmbientCache *ambientCache;
    WaterCache   *waterCache;

    Sound::Sample *sndSoundtrack;
    Sound::Sample *sndUnderwater;
    Sound::Sample *sndCurrent;

    int  curTrack;
    bool lastTitle;
    bool isEnded;

    TR::Effect effect;
    float      effectTimer;
    int        effectIdx;

// IGame implementation ========
    virtual void loadLevel(TR::LevelID id) {
        if (isEnded) return;
        Sound::stopAll();

        isEnded = true;
        char buf[64];
        buf[0] = 0;
        strcat(buf, "level/");
        strcat(buf, TR::LEVEL_INFO[id].name);
        switch (level.version) {
            case TR::VER_TR1_PC  : strcat(buf, ".PHD"); break;
            case TR::VER_TR1_PSX : strcat(buf, ".PSX"); break;
        }
        new Stream(buf, loadAsync);
    }

    virtual void loadNextLevel() {
        loadLevel(level.id == TR::LEVEL_10C ? TR::TITLE : TR::LevelID(level.id + 1));
    }

    virtual void loadGame(int slot) {
        //
    }

    virtual void saveGame(int slot) {
        //
    }

    virtual void applySettings(const Core::Settings &settings) {
        if (settings.detail.filter != Core::settings.detail.filter)
            atlas->setFilterQuality(settings.detail.filter);

        bool rebuildMesh    = settings.detail.water != Core::settings.detail.water;
        bool rebuildAmbient = settings.detail.lighting != Core::settings.detail.lighting;
        bool rebuildShadows = settings.detail.shadows  != Core::settings.detail.shadows;
        bool rebuildWater   = settings.detail.water != Core::settings.detail.water;
        bool rebuildShaders = rebuildWater || rebuildAmbient || rebuildShadows;

        Core::settings = settings;

        if (rebuildShaders) {
            delete shaderCache;
            shaderCache = new ShaderCache();
        }

        if (rebuildMesh) {
            delete mesh;
            mesh = new MeshBuilder(level);
        }

        if (rebuildAmbient) {
            delete ambientCache;
            ambientCache = Core::settings.detail.lighting > Core::Settings::MEDIUM ? new AmbientCache(this) : NULL;
        }

        if (rebuildShadows) {
            delete shadow;
            shadow = Core::settings.detail.shadows > Core::Settings::LOW ? new Texture(SHADOW_TEX_SIZE, SHADOW_TEX_SIZE, Texture::SHADOW, false) : NULL;
        }
            
        if (rebuildWater) {
            delete waterCache;
            waterCache = Core::settings.detail.water > Core::Settings::LOW ? new WaterCache(this) : NULL;
        }
    }

    virtual TR::Level* getLevel() {
        return &level;
    }

    virtual MeshBuilder* getMesh() {
        return mesh;
    }

    virtual ICamera* getCamera() {
        return camera;
    }

    virtual Controller* getLara() {
        return lara;
    }

    virtual bool isCutscene() {
        if (level.id == TR::TITLE) return false;
        return camera->state == Camera::STATE_CUTSCENE;
    }

    virtual uint16 getRandomBox(uint16 zone, uint16 *zones) { 
        ZoneCache::Item *item = zoneCache->getBoxes(zone, zones);
        return item->boxes[int(randf() * item->count)];
    }
    
    virtual uint16 findPath(int ascend, int descend, bool big, int boxStart, int boxEnd, uint16 *zones, uint16 **boxes) {
        return zoneCache->findPath(ascend, descend, big, boxStart, boxEnd, zones, boxes);
    }

    virtual void setClipParams(float clipSign, float clipHeight) {
        params->clipSign   = clipSign;
        params->clipHeight = clipHeight;
    }

    virtual void setWaterParams(float height) {
        params->waterHeight = height;
    }

    virtual void waterDrop(const vec3 &pos, float radius, float strength) {
        if (waterCache)
            waterCache->addDrop(pos, radius, strength);
    }

    virtual void setShader(Core::Pass pass, Shader::Type type, bool underwater = false, bool alphaTest = false) {
        shaderCache->bind(pass, type, (underwater ? ShaderCache::FX_UNDERWATER : 0) | (alphaTest ? ShaderCache::FX_ALPHA_TEST : 0) | ((params->clipHeight != NO_CLIP_PLANE && pass == Core::passCompose) ? ShaderCache::FX_CLIP_PLANE : 0), this);
    }

    virtual void setRoomParams(int roomIndex, Shader::Type type, float diffuse, float ambient, float specular, float alpha, bool alphaTest = false) {
        if (Core::pass == Core::passShadow) {
            setShader(Core::pass, type);
            return;
        }

        TR::Room &room = level.rooms[roomIndex];

        if (room.flags.water)
            setWaterParams(float(room.info.yTop));
        else
            setWaterParams(NO_CLIP_PLANE);

        setShader(Core::pass, type, room.flags.water, alphaTest);

        if (room.flags.water) {
            if (waterCache)
                waterCache->bindCaustics(roomIndex);
            setWaterParams(float(room.info.yTop));
        }

        Core::active.shader->setParam(uParam, Core::params);
        Core::active.shader->setParam(uMaterial, vec4(diffuse, ambient, specular, alpha));

        if (Core::settings.detail.shadows > Core::Settings::MEDIUM)
            Core::active.shader->setParam(uContacts, Core::contacts[0], MAX_CONTACTS);
    }

    virtual void setupBinding() {
        atlas->bind(sDiffuse);
        Core::whiteTex->bind(sNormal);
        Core::whiteTex->bind(sMask);
        Core::whiteTex->bind(sReflect);        
        cube->bind(sEnvironment);
        Core::basis.identity();
    }

    virtual void renderEnvironment(int roomIndex, const vec3 &pos, Texture **targets, int stride = 0) {
        PROFILE_MARKER("ENVIRONMENT");
        Core::eye = 0.0f;
        setupBinding();
    // first pass render level into cube faces
        for (int i = 0; i < 6; i++) {
            setupCubeCamera(pos, i);
            Core::pass = Core::passAmbient;
            Texture *target = targets[0]->cube ? targets[0] : targets[i * stride];
            Core::setTarget(target, true, i);
            renderView(roomIndex, false);
            Core::invalidateTarget(false, true);
        }
    }
    
    virtual void setEffect(Controller *controller, TR::Effect effect) {
        this->effect      = effect;
        this->effectTimer = 0.0f;
        this->effectIdx   = 0;

        switch (effect) {
            case TR::Effect::FLOOR_SHAKE :
                camera->shake = 0.5f * max(0.0f, 1.0f - (controller->pos - camera->pos).length2() / (15 * 1024 * 15 * 1024));
                return;
            case TR::Effect::FLOOD : {
                Sound::Sample *sample = playSound(TR::SND_FLOOD, vec3(), 0);
                if (sample)
                    sample->setVolume(0.0f, 4.0f);
                break;
            }
            case TR::Effect::STAIRS2SLOPE :
                playSound(TR::SND_STAIRS2SLOPE, vec3(), 0);
                break;
            case TR::Effect::EXPLOSION :
                playSound(TR::SND_TNT, vec3(0), 0);
                camera->shake = 1.0f;
                break;
            default : ;
        }
    }

    virtual void checkTrigger(Controller *controller, bool heavy) {
        lara->checkTrigger(controller, heavy);
    }

    virtual int addSprite(TR::Entity::Type type, int room, int x, int y, int z, int frame = -1, bool empty = false) {
        return Sprite::add(this, type, room, x, y, z, frame, empty);
    }

    virtual int addEntity(TR::Entity::Type type, int room, const vec3 &pos, float angle) {
        int index = level.entityAdd(type, room, int(pos.x), int(pos.y), int(pos.z), TR::angle(angle), -1);
        if (index > -1) {
            TR::Entity &e = level.entities[index];
            Controller *controller = initController(index);
            e.controller = controller;
            if (e.isEnemy()) {
                e.flags.active = TR::ACTIVE;
                controller->activate();
            }
            if (e.isPickup())
                e.intensity = 4096;
        }
        return index;
    }

    virtual bool invUse(TR::Entity::Type type) {
        if (!lara->useItem(type))
            return inventory.use(type);
        return true;
    }

    virtual void invAdd(TR::Entity::Type type, int count) {
        inventory.add(type, count);
    }

    virtual int* invCount(TR::Entity::Type type) { 
        return inventory.getCountPtr(type);
    }

    virtual bool invChooseKey(TR::Entity::Type hole) {
        return inventory.chooseKey(hole);
    }

    virtual Sound::Sample* playSound(int id, const vec3 &pos = vec3(0.0f), int flags = 0) const {
        if (level.version == TR::VER_TR1_PSX && id == TR::SND_SECRET)
            return NULL;

        int16 a = level.soundsMap[id];
        if (a == -1) return NULL;

        TR::SoundInfo &b = level.soundsInfo[a];
        if (b.chance == 0 || (rand() & 0x7fff) <= b.chance) {
            int   index  = b.offset + rand() % b.flags.count;
            float volume = (float)b.volume / 0x7FFF;
            float pitch  = b.flags.pitch ? (0.9f + randf() * 0.2f) : 1.0f;
            if (!(flags & Sound::MUSIC)) {
                switch (b.flags.mode) {
                    case 0 : flags |= Sound::UNIQUE; break;
                    case 1 : flags |= Sound::REPLAY; break;
                    case 2 : flags |= Sound::FLIPPED | Sound::UNFLIPPED | Sound::LOOP; break;
                }
            }
            if (b.flags.gain) volume = max(0.0f, volume - randf() * 0.25f);
            //if (b.flags.camera) flags &= ~Sound::PAN;
            return Sound::play(level.getSampleStream(index), pos, volume, pitch, flags, id);
        }
        return NULL;
    }

    void stopChannel(Sound::Sample *channel) {
        if (channel == sndSoundtrack) {
            if (sndCurrent == sndSoundtrack)
                sndCurrent = NULL;
            sndSoundtrack = NULL;
            playTrack(0);
        }
    }

    static void playAsync(Stream *stream, void *userData) {
        if (!stream) return;
        Level *level = (Level*)userData;

        level->sndSoundtrack = Sound::play(stream, vec3(0.0f), 0.01f, 1.0f, Sound::MUSIC);
        if (level->sndSoundtrack)
            level->sndSoundtrack->setVolume(1.0f, 0.2f);
    }

    virtual void playTrack(int track, bool restart = false) {
        if (track == 0)
            track = TR::LEVEL_INFO[level.id].ambientTrack;

        if (curTrack == track) {
            if (restart && sndSoundtrack) {
                sndSoundtrack->replay();
                sndSoundtrack->setVolume(1.0f, 0.2f);
            }
            return;
        }
        curTrack = track;

        if (sndSoundtrack) {
            sndSoundtrack->setVolume(-1.0f, 0.2f);
            if (sndCurrent == sndSoundtrack)
                sndCurrent = NULL;
            sndSoundtrack = NULL;
        }

        if (track <= 0) return;

        char title[32];
        sprintf(title, "audio/track_%02d.ogg", track);

        new Stream(title, playAsync, this);
    }

    virtual void stopTrack() {
        playTrack(-1);
    }
//==============================

    Level(Stream &stream) : level(stream), inventory(this), lara(NULL), isEnded(false) {
        params->time = 0.0f;

        #ifdef _DEBUG
            Debug::init();
        #endif
        
        initTextures();
        mesh = new MeshBuilder(level);

        initOverrides();

        for (int i = 0; i < level.entitiesBaseCount; i++) {
            TR::Entity &e = level.entities[i];
            e.controller = initController(i);
            if (e.type == TR::Entity::LARA || e.type == TR::Entity::CUT_1)
                lara = (Lara*)e.controller;
        }

        if (level.id != TR::TITLE) {
            ASSERT(lara != NULL);
            camera = new Camera(this, lara);

            level.cameraController = camera;
            level.laraController   = lara;

            zoneCache    = new ZoneCache(this);
            ambientCache = Core::settings.detail.lighting > Core::Settings::MEDIUM ? new AmbientCache(this) : NULL;
            waterCache   = Core::settings.detail.water    > Core::Settings::LOW    ? new WaterCache(this)   : NULL;
            shadow       = Core::settings.detail.shadows  > Core::Settings::LOW    ? new Texture(SHADOW_TEX_SIZE, SHADOW_TEX_SIZE, Texture::SHADOW, false) : NULL;

            initReflections();

        // init sounds
            //sndSoundtrack = Sound::play(Sound::openWAD("05_Lara's_Themes.wav"), vec3(0.0f), 1, 1, Sound::Flags::LOOP);

            sndUnderwater = playSound(TR::SND_UNDERWATER, vec3(0.0f), Sound::LOOP | Sound::MUSIC);
            if (sndUnderwater)
                sndUnderwater->volume = sndUnderwater->volumeTarget = 0.0f;

            for (int i = 0; i < level.soundSourcesCount; i++) {
                TR::SoundSource &src = level.soundSources[i];
                playSound(src.id, vec3(float(src.x), float(src.y), float(src.z)), Sound::PAN | src.flags);
            }

            lastTitle       = false;
        } else {
            camera          = NULL;
            ambientCache    = NULL;
            waterCache      = NULL;
            zoneCache       = NULL;
            shadow          = NULL;
            sndSoundtrack   = NULL;
            sndUnderwater   = NULL;
            sndCurrent      = NULL;
            lastTitle       = true;
            inventory.toggle(Inventory::PAGE_OPTION);
        }

        sndSoundtrack = NULL;
        playTrack(curTrack = 0);
        sndCurrent = sndSoundtrack;

        effect = TR::Effect::NONE;
    }

    virtual ~Level() {
        #ifdef _DEBUG
            Debug::free();
        #endif
        for (int i = 0; i < level.entitiesCount; i++)
            delete (Controller*)level.entities[i].controller;

        delete shadow;
        delete ambientCache;
        delete waterCache;
        delete zoneCache;

        delete atlas;
        delete cube;
        delete mesh;

        delete camera;
        Sound::stopAll();
    }

    Controller* initController(int index) {
        switch (level.entities[index].type) {
            case TR::Entity::LARA                  :
            case TR::Entity::CUT_1                 : return new Lara(this, index);
            case TR::Entity::ENEMY_DOPPELGANGER    : return new Doppelganger(this, index);
            case TR::Entity::ENEMY_WOLF            : return new Wolf(this, index);
            case TR::Entity::ENEMY_BEAR            : return new Bear(this, index);
            case TR::Entity::ENEMY_BAT             : return new Bat(this, index);
            case TR::Entity::ENEMY_LION_MALE       :
            case TR::Entity::ENEMY_LION_FEMALE     : return new Lion(this, index);
            case TR::Entity::ENEMY_RAT_LAND        :
            case TR::Entity::ENEMY_RAT_WATER       : return new Rat(this, index);
            case TR::Entity::ENEMY_REX             : return new Rex(this, index);
            case TR::Entity::ENEMY_RAPTOR          : return new Raptor(this, index);
            case TR::Entity::ENEMY_MUTANT_1        :
            case TR::Entity::ENEMY_MUTANT_2        :
            case TR::Entity::ENEMY_MUTANT_3        : return new Mutant(this, index);
            case TR::Entity::ENEMY_CENTAUR         : return new Centaur(this, index);
            case TR::Entity::ENEMY_MUMMY           : return new Mummy(this, index);
            case TR::Entity::ENEMY_CROCODILE_LAND  :
            case TR::Entity::ENEMY_CROCODILE_WATER :
            case TR::Entity::ENEMY_PUMA            :
            case TR::Entity::ENEMY_GORILLA         : return new Enemy(this, index, 100, 10, 0.0f, 0.0f);
            case TR::Entity::ENEMY_LARSON          : return new Larson(this, index);
            case TR::Entity::ENEMY_PIERRE          : return new Pierre(this, index);
            case TR::Entity::ENEMY_SKATEBOY        : return new SkaterBoy(this, index);
            case TR::Entity::ENEMY_COWBOY          : return new Cowboy(this, index);
            case TR::Entity::ENEMY_MR_T            : return new MrT(this, index);
            case TR::Entity::ENEMY_NATLA           : return new Natla(this, index);
            case TR::Entity::ENEMY_GIANT_MUTANT    : return new GiantMutant(this, index);
            case TR::Entity::DOOR_1                :
            case TR::Entity::DOOR_2                :
            case TR::Entity::DOOR_3                :
            case TR::Entity::DOOR_4                :
            case TR::Entity::DOOR_5                :
            case TR::Entity::DOOR_6                :
            case TR::Entity::DOOR_BIG_1            :
            case TR::Entity::DOOR_BIG_2            : return new Door(this, index);
            case TR::Entity::TRAP_DOOR_1           :
            case TR::Entity::TRAP_DOOR_2           : return new TrapDoor(this, index);
            case TR::Entity::BRIDGE_0              :
            case TR::Entity::BRIDGE_1              :
            case TR::Entity::BRIDGE_2              : return new Bridge(this, index);
            case TR::Entity::GEARS_1               :
            case TR::Entity::GEARS_2               :
            case TR::Entity::GEARS_3               : return new Gear(this, index);
            case TR::Entity::TRAP_FLOOR            : return new TrapFloor(this, index);
            case TR::Entity::CRYSTAL               : return new Crystal(this, index);
            case TR::Entity::TRAP_BLADE            : return new TrapBlade(this, index);
            case TR::Entity::TRAP_SPIKES           : return new TrapSpikes(this, index);
            case TR::Entity::TRAP_BOULDER          : return new TrapBoulder(this, index);
            case TR::Entity::TRAP_DART_EMITTER     : return new TrapDartEmitter(this, index);
            case TR::Entity::DRAWBRIDGE            : return new Drawbridge(this, index);
            case TR::Entity::BLOCK_1               :
            case TR::Entity::BLOCK_2               :
            case TR::Entity::BLOCK_3               :
            case TR::Entity::BLOCK_4               : return new Block(this, index);
            case TR::Entity::MOVING_BLOCK          : return new MovingBlock(this, index);
            case TR::Entity::TRAP_CEILING_1        :
            case TR::Entity::TRAP_CEILING_2        : return new TrapCeiling(this, index);
            case TR::Entity::TRAP_SLAM             : return new TrapSlam(this, index);
            case TR::Entity::TRAP_SWORD            : return new TrapSword(this, index);
            case TR::Entity::HAMMER_HANDLE         : return new ThorHammer(this, index);
            case TR::Entity::LIGHTNING             : return new Lightning(this, index);
            case TR::Entity::DOOR_LATCH            : return new DoorLatch(this, index);
            case TR::Entity::SWITCH                :
            case TR::Entity::SWITCH_WATER          : return new Switch(this, index);
            case TR::Entity::PUZZLE_HOLE_1         :
            case TR::Entity::PUZZLE_HOLE_2         :
            case TR::Entity::PUZZLE_HOLE_3         :
            case TR::Entity::PUZZLE_HOLE_4         :
            case TR::Entity::KEY_HOLE_1            :
            case TR::Entity::KEY_HOLE_2            :
            case TR::Entity::KEY_HOLE_3            :
            case TR::Entity::KEY_HOLE_4            : return new KeyHole(this, index);
            case TR::Entity::MIDAS_HAND            : return new MidasHand(this, index);
            case TR::Entity::SCION_TARGET          : return new ScionTarget(this, index);
            case TR::Entity::WATERFALL             : return new Waterfall(this, index);
            case TR::Entity::TRAP_LAVA             : return new TrapLava(this, index);
            case TR::Entity::CENTAUR_STATUE        : return new CentaurStatue(this, index);
            case TR::Entity::CABIN                 : return new Cabin(this, index);
            case TR::Entity::TRAP_FLAME_EMITTER    : return new TrapFlameEmitter(this, index);
            case TR::Entity::BOAT                  : return new Boat(this, index);
            case TR::Entity::EARTHQUAKE            : return new Earthquake(this, index);
            case TR::Entity::MUTANT_EGG_SMALL      :
            case TR::Entity::MUTANT_EGG_BIG        : return new MutantEgg(this, index);
            default                                : return (level.entities[index].modelIndex > 0) ? new Controller(this, index) : new Sprite(this, index, 0);
        }
    }

    static void fillCallback(int id, int width, int height, int tileX, int tileY, void *userData, void *data) {
        static const uint32 barColor[UI::BAR_MAX][25] = {
            // flash bar
                { 0x00000000, 0xFFA20058, 0xFFFFFFFF, 0xFFA20058, 0x00000000 },
            // health bar
                { 0xFF2C5D71, 0xFF5E81AE, 0xFF2C5D71, 0xFF1B4557, 0xFF16304F },
            // oxygen bar
                { 0xFF647464, 0xFFA47848, 0xFF647464, 0xFF4C504C, 0xFF303030 },
            // option bar
                { 0x00FFFFFF, 0x20FFFFFF, 0x20FFFFFF, 0x20FFFFFF, 0x00FFFFFF,
                  0x00FFFFFF, 0x60FFFFFF, 0x60FFFFFF, 0x60FFFFFF, 0x00FFFFFF,
                  0x00FFFFFF, 0x80FFFFFF, 0x80FFFFFF, 0x80FFFFFF, 0x00FFFFFF,
                  0x00FFFFFF, 0x60FFFFFF, 0x60FFFFFF, 0x60FFFFFF, 0x00FFFFFF,
                  0x00FFFFFF, 0x20FFFFFF, 0x20FFFFFF, 0x20FFFFFF, 0x00FFFFFF },
            // white bar (white tile)
                { 0xFFFFFFFF },
            };

        int stride = 256, uvCount;
        short2 *uv = NULL;

        TR::Level *level = (TR::Level*)userData;
        TR::Color32 *src, *dst = (TR::Color32*)data;
        short4 mm;

        if (id < level->objectTexturesCount) { // textures
            TR::ObjectTexture &t = level->objectTextures[id];
            mm      = t.getMinMax();
            src     = level->tiles[t.tile.index].color;
            uv      = t.texCoord;
            uvCount = 4;
        } else {
            id -= level->objectTexturesCount;

            if (id < level->spriteTexturesCount) { // sprites
                TR::SpriteTexture &t = level->spriteTextures[id];
                mm      = t.getMinMax();
                src     = level->tiles[t.tile].color;
                uv      = t.texCoord;
                uvCount = 2;
            } else { // common (generated) textures
                id -= level->spriteTexturesCount;
                TR::ObjectTexture *tex;
                mm.x = mm.y = mm.z = mm.w = 0;
                stride  = 1;
                uvCount = 4;

                switch (id) {
                    case UI::BAR_FLASH    :
                    case UI::BAR_HEALTH   :
                    case UI::BAR_OXYGEN   : 
                    case UI::BAR_OPTION   :
                    case UI::BAR_WHITE    :
                        src  = (TR::Color32*)&barColor[id][0];
                        tex  = &barTile[id];
                        if (id != UI::BAR_WHITE) {
                            mm.w = 4; // height - 1
                            if (id == UI::BAR_OPTION) {
                                stride = 5;
                                mm.z   = 4;
                            }
                        }
                        break;
                    default : return;
                }

                memset(tex, 0, sizeof(*tex));
                uv = tex->texCoord;
                uv[2].y += mm.w;
                uv[3].y += mm.w;
                uv[1].x += mm.z;
                uv[2].x += mm.z;
            }
        }

        int cx = -mm.x, cy = -mm.y;

        if (data) {
            int w = mm.z - mm.x + 1;
            int h = mm.w - mm.y + 1;
            int dstIndex = tileY * width + tileX;
            for (int y = -ATLAS_BORDER; y < h + ATLAS_BORDER; y++) {
                for (int x = -ATLAS_BORDER; x < w + ATLAS_BORDER; x++) {
                    TR::Color32 *p = &src[mm.y * stride + mm.x];
                    p += clamp(x, 0, w - 1);
                    p += clamp(y, 0, h - 1) * stride;
                    dst[dstIndex++] = *p;
                }
                dstIndex += width - ATLAS_BORDER * 2 - w;
            }

            cx += tileX + ATLAS_BORDER;
            cy += tileY + ATLAS_BORDER;
        }

        for (int i = 0; i < uvCount; i++) {
            if (uv[i].x == mm.z) uv[i].x++;
            if (uv[i].y == mm.w) uv[i].y++;
            uv[i].x += cx;
            uv[i].y += cy;

            uv[i].x = int32(uv[i].x) * 32767 / width;
            uv[i].y = int32(uv[i].y) * 32767 / height;
        }

    // apply ref for instanced tile
        if (data) return;

        int ref = tileX;
        if (ref < level->objectTexturesCount) { // textures
            mm = level->objectTextures[ref].getMinMax();
        } else {
            ref -= level->objectTexturesCount;
            if (ref < level->spriteTexturesCount) // sprites
                mm = level->spriteTextures[ref].getMinMax();
            else
                ASSERT(false); // only object textures and sprites may be instanced
        }

        for (int i = 0; i < uvCount; i++) {
            uv[i].x += mm.x;
            uv[i].y += mm.y;
        }
    }

    void initTextures() {
        if (!level.tilesCount) {
            atlas = NULL;
            return;
        }

    // repack texture tiles
        Atlas *tiles = new Atlas(level.objectTexturesCount + level.spriteTexturesCount + UI::BAR_MAX, &level, fillCallback);
        // add textures
        int texIdx = level.version == TR::VER_TR1_PSX ? 256 : 0; // skip palette color for PSX version
        for (int i = texIdx; i < level.objectTexturesCount; i++) {
            TR::ObjectTexture &t = level.objectTextures[i];
            int16 tx = (t.tile.index % 4) * 256;
            int16 ty = (t.tile.index / 4) * 256;

            short4 uv;
            uv.x = tx + min(min(t.texCoord[0].x, t.texCoord[1].x), t.texCoord[2].x);
            uv.y = ty + min(min(t.texCoord[0].y, t.texCoord[1].y), t.texCoord[2].y);
            uv.z = tx + max(max(t.texCoord[0].x, t.texCoord[1].x), t.texCoord[2].x) + 1;
            uv.w = ty + max(max(t.texCoord[0].y, t.texCoord[1].y), t.texCoord[2].y) + 1;

            tiles->add(uv, texIdx++);
        }
        // add sprites
        for (int i = 0; i < level.spriteTexturesCount; i++) {
            TR::SpriteTexture &t = level.spriteTextures[i];
            int16 tx = (t.tile % 4) * 256;
            int16 ty = (t.tile / 4) * 256;

            short4 uv;
            uv.x = tx + t.texCoord[0].x;
            uv.y = ty + t.texCoord[0].y;
            uv.z = tx + t.texCoord[1].x + 1;
            uv.w = ty + t.texCoord[1].y + 1;

            tiles->add(uv, texIdx++);
        }
        // add common textures
        const short2 bar[UI::BAR_MAX] = { {0, 4}, {0, 4}, {0, 4}, {4, 4}, {0, 0} };
        for (int i = 0; i < UI::BAR_MAX; i++)
            tiles->add(short4(i * 32, 4096, i * 32 + bar[i].x, 4096 + bar[i].y), texIdx++);

        // get result texture
        atlas = tiles->pack();
        atlas->setFilterQuality(Core::settings.detail.filter);

        delete tiles;

        LOG("atlas: %d x %d\n", atlas->width, atlas->height);
        PROFILE_LABEL(TEXTURE, atlas->ID, "atlas");

        uint32 whitePix = 0xFFFFFFFF;
        cube = new Texture(1, 1, Texture::RGBA, true, &whitePix);

        delete[] level.tiles;
        level.tiles = NULL;
    }

    void initOverrides() {
    /*
        for (int i = 0; i < level.entitiesCount; i++) {
            int16 &id = level.entities[i].id;
            switch (id) {
            // weapon
                case 84 : id =  99; break; // pistols
                case 85 : id = 100; break; // shotgun
                case 86 : id = 101; break; // magnums
                case 87 : id = 102; break; // uzis
            // ammo
                case 88 : id = 103; break; // for pistols
                case 89 : id = 104; break; // for shotgun
                case 90 : id = 105; break; // for magnums
                case 91 : id = 106; break; // for uzis
            // medikit
                case 93 : id = 108; break; // big
                case 94 : id = 109; break; // small
            // keys
                case 110 : id = 114; break; 
                case 111 : id = 115; break; 
                case 112 : id = 116; break; 
                case 113 : id = 117; break; 
                case 126 : id = 127; break; 
                case 129 : id = 133; break; 
                case 130 : id = 134; break; 
                case 131 : id = 135; break; 
                case 132 : id = 136; break; 
                case 141 : id = 145; break; 
                case 142 : id = 146; break; 
                case 143 : id = 150; break; 
                case 144 : id = 150; break;
            }
        }
    */
    }

    void initReflections() {
        Core::beginFrame();
        for (int i = 0; i < level.entitiesBaseCount; i++) {
            TR::Entity &e = level.entities[i];
            if (e.type == TR::Entity::CRYSTAL) {
                Crystal *c = (Crystal*)e.controller;
                renderEnvironment(c->getRoomIndex(), c->pos - vec3(0, 512, 0), &c->environment);
                c->environment->generateMipMap();
            }
        }
        Core::endFrame();
    }

    void setMainLight(Controller *controller) {
        Core::lightPos[0]   = controller->mainLightPos;
        Core::lightColor[0] = vec4(controller->mainLightColor.xyz, 1.0f / controller->mainLightColor.w);
    }

    void renderRooms(int *roomsList, int roomsCount) {
        PROFILE_MARKER("ROOMS");

        for (int i = 0; i < level.roomsCount; i++)
            level.rooms[i].flags.visible = false;

        for (int i = 0; i < roomsCount; i++)
            level.rooms[roomsList[i]].flags.visible = true;

        if (Core::pass == Core::passShadow)
            return;

        if (Core::settings.detail.shadows > Core::Settings::MEDIUM) {
            Sphere spheres[MAX_CONTACTS];
            int spheresCount;
            lara->getSpheres(spheres, spheresCount);

            for (int i = 0; i < MAX_CONTACTS; i++)
                if (i < spheresCount)
                    Core::contacts[i] = vec4(spheres[i].center, PI * spheres[i].radius * spheres[i].radius * 0.25f);
                else
                    Core::contacts[i] = vec4(0.0f);
        }

        setMainLight(lara);

        bool hasGeom[2], hasSprite;
        hasGeom[0] = hasGeom[1] = hasSprite = false;

        Basis basis;
        basis.identity();

        Core::setBlending(bmNone);
        for (int transp = 0; transp < 2; transp++) {
            for (int i = 0; i < roomsCount; i++) {
                int roomIndex = roomsList[i];
                MeshBuilder::RoomRange &range = mesh->rooms[roomIndex];

                if (!range.geometry[transp].iCount)
                    continue;

                setRoomParams(roomIndex, Shader::ROOM, 1.0f, intensityf(level.rooms[roomIndex].ambient), 0.0f, 1.0f, transp > 0);
                Shader *sh = Core::active.shader;

                if (!hasGeom[transp]) {
                    hasGeom[transp] = true;
                    sh->setParam(uLightColor, Core::lightColor[0], MAX_LIGHTS);
                    sh->setParam(uLightPos,   Core::lightPos[0],   MAX_LIGHTS);
                }

                basis.pos = level.rooms[roomIndex].getOffset();
                sh->setParam(uBasis, basis);
                mesh->renderRoomGeometry(roomIndex, transp > 0);
            }
            Core::setBlending(bmAlpha);
        }
 
        basis.rot = Core::mViewInv.getRot();
        for (int i = 0; i < roomsCount; i++) {
            level.rooms[roomsList[i]].flags.visible = true;

            int roomIndex = roomsList[i];
            MeshBuilder::RoomRange &range = mesh->rooms[roomIndex];

            if (!range.sprites.iCount)
                continue;

            setRoomParams(roomIndex, Shader::SPRITE, 1.0f, 1.0f, 0.0f, 1.0f, true);
            Shader *sh = Core::active.shader;
            if (!hasSprite) {
                sh->setParam(uLightColor, Core::lightColor[0], MAX_LIGHTS);
                sh->setParam(uLightPos,   Core::lightPos[0],   MAX_LIGHTS);
            }

            basis.pos = level.rooms[roomIndex].getOffset();
            sh->setParam(uBasis, basis);
            mesh->renderRoomSprites(roomIndex);
        }

        Core::setBlending(bmNone);
    }

    void renderEntity(const TR::Entity &entity) {
        //if (entity.room != lara->getRoomIndex()) return;
        if (entity.type == TR::Entity::NONE || !entity.modelIndex) return;
        if (Core::pass == Core::passShadow && !entity.castShadow()) return;

        ASSERT(entity.controller);

        bool isModel = entity.modelIndex > 0;

        Controller *controller = (Controller*)entity.controller;

        int roomIndex = controller->getRoomIndex();
        TR::Room &room = level.rooms[roomIndex];

        if (!entity.isLara() && !entity.isActor())
            if (!room.flags.visible || entity.flags.invisible || entity.flags.rendered)
                return;

        int16 lum = entity.intensity == -1 ? room.ambient : entity.intensity;

        Shader::Type type = isModel ? Shader::ENTITY : Shader::SPRITE;
        if (entity.type == TR::Entity::CRYSTAL)
            type = Shader::MIRROR;

        if (type == Shader::SPRITE) {
            float alpha = (entity.type == TR::Entity::SMOKE || entity.type == TR::Entity::WATER_SPLASH) ? 0.75f : 1.0f;
            float diffuse = entity.isPickup() ? 1.0f : 0.5f;
            setRoomParams(roomIndex, type, diffuse, intensityf(lum), controller->specular, alpha, isModel ? !mesh->models[entity.modelIndex - 1].opaque : true);
        } else
            setRoomParams(roomIndex, type, 1.0f, intensityf(lum), controller->specular, 1.0f, isModel ? !mesh->models[entity.modelIndex - 1].opaque : true);

        if (isModel) { // model
            vec3 pos = controller->getPos();
            if (ambientCache) {
                AmbientCache::Cube cube;
                if (Core::stats.frame != controller->frameIndex) {
                    ambientCache->getAmbient(roomIndex, pos, cube);
                    if (cube.status == AmbientCache::Cube::READY)
                        memcpy(controller->ambient, cube.colors, sizeof(cube.colors)); // store last calculated ambient into controller
                }
                Core::active.shader->setParam(uAmbient, controller->ambient[0], 6);
            }

            setMainLight(controller);
        } else { // sprite
            Core::lightPos[0]   = vec3(0);
            Core::lightColor[0] = vec4(0, 0, 0, 1);
        }        
        
        Core::active.shader->setParam(uLightPos,   Core::lightPos[0],   MAX_LIGHTS);
        Core::active.shader->setParam(uLightColor, Core::lightColor[0], MAX_LIGHTS);

        controller->render(camera->frustum, mesh, type, room.flags.water);
    }

    void update() {
        if (isCutscene() && (lara->health > 0.0f && !sndSoundtrack))
            return;

        if (Input::state[cInventory] && level.id != TR::TITLE) {
            if (lara->health <= 0.0f)
                inventory.toggle(Inventory::PAGE_OPTION, TR::Entity::INV_PASSPORT);
            else
                inventory.toggle();
        }

        Sound::Sample *sndChanged = sndCurrent;

        if (inventory.isActive() || level.id == TR::TITLE) {
            Sound::reverb.setRoomSize(vec3(1.0f));
            inventory.update();
            if (level.id != TR::TITLE)
                sndChanged = NULL;
        } else {
            params->time += Core::deltaTime;

            updateEffect();

            Controller *c = Controller::first;
            while (c) {
                Controller *next = c->next;
                c->update();
                c = next;
            }

            if (!isCutscene() && camera->state != Camera::STATE_STATIC)
                camera->state = lara->emptyHands() ? Camera::STATE_FOLLOW : Camera::STATE_COMBAT;

            camera->update();

            if (waterCache) 
                waterCache->update();

            Controller::clearInactive();

            sndChanged = camera->isUnderwater() ? sndUnderwater : sndSoundtrack;
        }

        if (sndChanged != sndCurrent) {
            if (sndCurrent) sndCurrent->setVolume(0.0f, 0.2f);
            if (sndChanged) sndChanged->setVolume(1.0f, 0.2f);
            sndCurrent = sndChanged;
        }
    }

    void updateEffect() {
        if (effect == TR::Effect::NONE)
            return;

        effectTimer += Core::deltaTime;

        switch (effect) {
            case TR::Effect::FLICKER : {
                int idx = effectIdx;
                switch (effectIdx) {
                    case 0 : if (effectTimer > 3.0f) effectIdx++; break;
                    case 1 : if (effectTimer > 3.1f) effectIdx++; break;
                    case 2 : if (effectTimer > 3.5f) effectIdx++; break;
                    case 3 : if (effectTimer > 3.6f) effectIdx++; break;
                    case 4 : if (effectTimer > 4.1f) { effectIdx++; effect = TR::Effect::NONE; } break;
                }
                if (idx != effectIdx)
                    level.isFlipped = !level.isFlipped;
                break;
            }
            case TR::Effect::EARTHQUAKE : {
                switch (effectIdx) {
                    case 0 : if (effectTimer > 0.0f) { playSound(TR::SND_ROCK);     effectIdx++; camera->shake = 1.0f; } break;
                    case 1 : if (effectTimer > 0.1f) { playSound(TR::SND_STOMP);    effectIdx++; } break;
                    case 2 : if (effectTimer > 0.6f) { playSound(TR::SND_BOULDER);  effectIdx++; camera->shake += 0.5f; } break;
                    case 3 : if (effectTimer > 1.1f) { playSound(TR::SND_ROCK);     effectIdx++; } break;
                    case 4 : if (effectTimer > 1.6f) { playSound(TR::SND_BOULDER);  effectIdx++; camera->shake += 0.5f; } break;
                    case 5 : if (effectTimer > 2.3f) { playSound(TR::SND_BOULDER);  camera->shake += 0.5f; effect = TR::Effect::NONE; } break;
                }
                break;
            }
            default : effect = TR::Effect::NONE; return;
        }
    }

    void setup() {
        PROFILE_MARKER("SETUP");

        camera->setup(Core::pass == Core::passCompose);

        setupBinding();
    }

    // TODO: opqque/transparent pass for rooms and entities
    void renderEntities(bool opaque) {
        for (int i = 0; i < level.entitiesCount; i++) {
            int modelIndex = level.entities[i].modelIndex;
            if ((modelIndex < 0 && !opaque) || (modelIndex > 0 && mesh->models[modelIndex - 1].opaque == opaque))
                renderEntity(level.entities[i]);
        }
    }

    void renderEntities() {
        PROFILE_MARKER("ENTITIES");
        renderEntities(true);
        renderEntities(false);

        for (int i = 0; i < level.entitiesCount; i++) {
            TR::Entity &entity = level.entities[i];
            if (entity.controller && entity.flags.rendered)
                ((Controller*)entity.controller)->renderShadow(mesh);
        }
    }

    bool checkPortal(const TR::Room &room, const TR::Room::Portal &portal, const vec4 &viewPort, vec4 &clipPort) {
        vec3 n = portal.normal;
        vec3 v = Core::viewPos - (room.getOffset() + portal.vertices[0]);

        if (n.dot(v) <= 0.0f)
            return false;

        int  zClip = 0;
        vec4 p[4];

        clipPort = vec4(INF, INF, -INF, -INF);

        for (int i = 0; i < 4; i++) {
            p[i] = Core::mViewProj * vec4(vec3(portal.vertices[i]) + room.getOffset(), 1.0f);

            if (p[i].w > 0.0f) {
                p[i].xyz *= (1.0f / p[i].w);

                clipPort.x = min(clipPort.x, p[i].x);
                clipPort.y = min(clipPort.y, p[i].y);
                clipPort.z = max(clipPort.z, p[i].x);
                clipPort.w = max(clipPort.w, p[i].y);
            } else
                zClip++;
        }

        if (zClip == 4)
            return false;

        if (zClip > 0) {
            for (int i = 0; i < 4; i++) {
                vec4 &a = p[i];
                vec4 &b = p[(i + 1) % 4];

                if ((a.w > 0.0f) ^ (b.w > 0.0f)) {

                    if (a.x < 0.0f && b.x < 0.0f)
                        clipPort.x = -1.0f;
                    else
                        if (a.x > 0.0f && b.x > 0.0f)
                            clipPort.z = 1.0f;
                        else {
                            clipPort.x = -1.0f;
                            clipPort.z =  1.0f;
                        }

                    if (a.y < 0.0f && b.y < 0.0f)
                        clipPort.y = -1.0f;
                    else
                        if (a.y > 0.0f && b.y > 0.0f)
                            clipPort.w = 1.0f;
                        else {
                            clipPort.y = -1.0f;
                            clipPort.w =  1.0f;
                        }

                }
            }
        }

        if (clipPort.x > clipPort.z || clipPort.y > clipPort.w)
            return false;

        if (clipPort.x > viewPort.z || clipPort.y > viewPort.w || clipPort.z < viewPort.x || clipPort.w < viewPort.y)
            return false;

        clipPort.x = max(clipPort.x, viewPort.x);
        clipPort.y = max(clipPort.y, viewPort.y);
        clipPort.z = min(clipPort.z, viewPort.z);
        clipPort.w = min(clipPort.w, viewPort.w);

        return true;
    }

    void getVisibleRooms(int *roomsList, int &roomsCount, int from, int to, const vec4 &viewPort, bool water, int count = 0) {
        if (count > 16) {
            //ASSERT(false);
            return;
        }

        if (level.rooms[to].alternateRoom > -1 && level.isFlipped)
            to = level.rooms[to].alternateRoom;

        TR::Room &room = level.rooms[to];

        if (!room.flags.visible) {
            if (Core::pass == Core::passCompose && water && waterCache && from != TR::NO_ROOM && (level.rooms[from].flags.water ^ level.rooms[to].flags.water))
                waterCache->setVisible(from, to);

            room.flags.visible = true;
            roomsList[roomsCount++] = to;
        }

        vec4 clipPort;
        for (int i = 0; i < room.portalsCount; i++) {
            TR::Room::Portal &p = room.portals[i];

            if (from != room.portals[i].roomIndex && checkPortal(room, p, viewPort, clipPort))
                getVisibleRooms(roomsList, roomsCount, to, p.roomIndex, clipPort, water, count + 1);
        }
    }

    virtual void renderView(int roomIndex, bool water) {
        PROFILE_MARKER("VIEW");
        Core::Pass pass = Core::pass;

        if (water && waterCache) {
            waterCache->reset();
        }

        for (int i = 0; i < level.roomsCount; i++)
            level.rooms[i].flags.visible = false;

        int roomsList[128];
        int roomsCount = 0;

        getVisibleRooms(roomsList, roomsCount, TR::NO_ROOM, roomIndex, vec4(-1.0f, -1.0f, 1.0f, 1.0f), water);

        if (water && waterCache) {
            for (int i = 0; i < roomsCount; i++)
                waterCache->setVisible(roomsList[i]);
            waterCache->renderReflect();
            waterCache->simulate();
        }

        // clear visibility flag for rooms
        if (Core::pass != Core::passAmbient)
            for (int i = 0; i < level.entitiesCount; i++)
                level.entities[i].flags.rendered = false;

        if (water) {
            Core::setTarget(NULL, true); // render to back buffer
            setupBinding();
        }

        camera->setup(Core::pass == Core::passCompose);

        Core::pass = pass;

        renderRooms(roomsList, roomsCount);

        if (Core::pass != Core::passAmbient)
            renderEntities();

        if (water && waterCache && waterCache->visible) {
            waterCache->renderMask();
            waterCache->getRefract();
            setMainLight(lara);
            waterCache->render();
        }
    }

    void setupCubeCamera(const vec3 &pos, int face) {
        vec3 up  = vec3(0, -1, 0);
        vec3 dir;
        switch (face) {
            case 0 : dir = vec3( 1,  0,  0); break;
            case 1 : dir = vec3(-1,  0,  0); break;
            case 2 : dir = vec3( 0,  1,  0); up = vec3(0, 0,  1); break;
            case 3 : dir = vec3( 0, -1,  0); up = vec3(0, 0, -1); break;
            case 4 : dir = vec3( 0,  0,  1); break;
            case 5 : dir = vec3( 0,  0, -1); break;
        }

        Core::mViewInv  = mat4(pos, pos + dir, up);
        Core::mView     = Core::mViewInv.inverse();
        Core::mProj     = mat4(90, 1.0f, camera->znear, camera->zfar);
        Core::mViewProj = Core::mProj * Core::mView;
    }

    void setupLightCamera() {
        vec3 pos = lara->getBoundingBox().center();

        Core::mViewInv = mat4(lara->mainLightPos, pos, vec3(0, -1, 0));
        Core::mView    = Core::mViewInv.inverse();
        Core::mProj    = mat4(90.0f, 1.0f, camera->znear, lara->mainLightColor.w * 1.5f);

        mat4 bias;
        bias.identity();
        bias.e03 = bias.e13 = bias.e23 = bias.e00 = bias.e11 = bias.e22 = 0.5f;

        Core::mLightProj =  bias * (Core::mProj * Core::mView);
    }

    void renderShadows(int roomIndex) {
        PROFILE_MARKER("PASS_SHADOW");
        Core::eye = 0.0f;
        Core::pass = Core::passShadow;
        shadow->unbind(sShadow);
        bool colorShadow = shadow->format == Texture::Format::RGBA ? true : false;
        if (colorShadow)
            Core::setClearColor(vec4(1.0f));
        Core::setTarget(shadow, true);
        setupLightCamera();
        Core::setCulling(cfBack);

        setup();
        renderView(roomIndex, false);

        Core::invalidateTarget(!colorShadow, colorShadow);
        Core::setCulling(cfFront);
        if (colorShadow)
            Core::setClearColor(vec4(0.0f));
    }

    #ifdef _DEBUG
    void renderDebug() {
        if (level.id == TR::TITLE) return;

//        Core::mViewInv = camera->mViewInv;
//        Core::mView = Core::mViewInv.inverse();
        Core::setViewport(0, 0, Core::width, Core::height);
        camera->setup(true);
        
        if (Input::down[ikF]) {
            level.isFlipped = !level.isFlipped;
            Input::down[ikF] = false;
        }

        /*        
        static int snd_index = 0;
        if (Input::down[ikG]) {
            snd_index = (snd_index + 1) % level.soundsInfoCount;
            LOG("play sound: %d\n", snd_index);
            lara->playSound(snd_index, lara->pos, 0);
            Input::down[ikG] = false;
        }
        
        static int modelIndex = 0;
        static bool lastStateK = false;
        static int lastEntity = -1;
        if (Input::down[ikM]) {
            if (!lastStateK) {
                lastStateK = true;
                modelIndex = (modelIndex + 1) % level.modelsCount;
            //    modelIndex = (modelIndex + 1) % level.spriteSequencesCount;
                LOG("model: %d %d\n", modelIndex, level.spriteSequences[modelIndex].type);
                if (lastEntity > -1) {
                    delete level.entities[lastEntity].controller;
                    level.entityRemove(lastEntity);
                }
                vec3 p = lara->pos + lara->getDir() * 256.0f;
                lastEntity = level.entityAdd(level.models[modelIndex].type, lara->getRoomIndex(), p.x, p.y - 512, p.z, lara->getEntity().rotation, -1);
                level.entities[lastEntity].controller = new Controller(this, lastEntity);
            }
        } else
            lastStateK = false;

        if (lastEntity > -1)
            renderEntity(level.entities[lastEntity]);
//        renderModel(level.models[modelIndex], level.entities[4]);
*/
        Debug::begin();
        /*
        lara->updateEntity(); // TODO clip angle while rotating

        int q = int(normalizeAngle(lara->angleExt + PI * 0.25f) / (PI * 0.5f));
        float radius = 256.0f;

        const vec2 v[] = {
            { -radius,  radius },
            {  radius,  radius },
            {  radius, -radius },
            { -radius, -radius },
        };

        const vec2 &l = v[q],
                   &r = v[(q + 1) % 4],
                   &f = (q %= 2) ? vec2(l.x, radius * cosf(lara->angleExt)) : vec2(radius * sinf(lara->angleExt), l.y);

        vec3 F = vec3(f.x, 0.0f, f.y);
        vec3 L = vec3(l.x, 0.0f, l.y);
        vec3 R = vec3(r.x, 0.0f, r.y);

        vec3 p, n = lara->pos + vec3(0.0f, -512.0f, 0.0f);
        
        Core::setDepthTest(false);
        glBegin(GL_LINES);
            glColor3f(0, 0, 1); p = n; glVertex3fv((GLfloat*)&p); p += F; glVertex3fv((GLfloat*)&p);
            glColor3f(1, 0, 0); p = n; glVertex3fv((GLfloat*)&p); p += L; glVertex3fv((GLfloat*)&p);
            glColor3f(0, 1, 0); p = n; glVertex3fv((GLfloat*)&p); p += R; glVertex3fv((GLfloat*)&p);
            glColor3f(1, 1, 0); p = lara->pos; glVertex3fv((GLfloat*)&p); p -= vec3(0.0f, LARA_HANG_OFFSET, 0.0f); glVertex3fv((GLfloat*)&p);
        glEnd();
        Core::setDepthTest(true);
        */

        /*
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();
            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(0, Core::width, 0, Core::height, 0, 1);

            if (waterCache && waterCache->count && waterCache->items[0].caustics)
                waterCache->items[0].caustics->bind(sDiffuse);
            else
                atlas->bind(sDiffuse);
            glEnable(GL_TEXTURE_2D);
            Core::setCulling(cfNone);
            Core::setDepthTest(false);
            Core::setBlending(bmNone);
            Core::validateRenderState();

            glColor3f(10, 10, 10);
            int w = Core::active.textures[sDiffuse]->width / 2;
            int h = Core::active.textures[sDiffuse]->height / 2;
            glBegin(GL_QUADS);
                glTexCoord2f(0, 0); glVertex2f(0, 0);
                glTexCoord2f(1, 0); glVertex2f(w, 0);
                glTexCoord2f(1, 1); glVertex2f(w, h);
                glTexCoord2f(0, 1); glVertex2f(0, h);
            glEnd();
            glColor3f(1, 1, 1);

            glDisable(GL_TEXTURE_2D);

            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();
        */
           
        /*
        Core::setDepthTest(false);
        glBegin(GL_LINES);
            glColor3f(1, 1, 1);
            glVertex3fv((GLfloat*)&lara->pos);
            glVertex3fv((GLfloat*)&lara->mainLightPos);
        glEnd();
        Core::setDepthTest(true);
        */
        //    Debug::Draw::sphere(lara->mainLightPos, lara->mainLightColor.w, vec4(1, 1, 0, 1));

        //    Box bbox = lara->getBoundingBox();
        //    Debug::Draw::box(bbox.min, bbox.max, vec4(1, 0, 1, 1));

        //    Core::setBlending(bmAlpha);
        //    Debug::Level::rooms(level, lara->pos, lara->getEntity().room);
        //    Debug::Level::lights(level, lara->getRoomIndex(), lara);
        //    Debug::Level::sectors(level, lara->getRoomIndex(), (int)lara->pos.y);
        //    Core::setDepthTest(false);
        //    Debug::Level::portals(level);
        //    Core::setDepthTest(true);
        //    Debug::Level::meshes(level);
        //    Debug::Level::entities(level);
        //    Debug::Level::zones(level, lara);
        //    Debug::Level::blocks(level);
        //    Debug::Level::path(level, (Enemy*)level.entities[86].controller);
        //    Debug::Level::debugOverlaps(level, lara->box);
        //    Core::setBlending(bmNone);

        /*
            static int dbg_ambient = 0;
            dbg_ambient = int(params->time * 2) % 4;

            shadow->unbind(sShadow);
            cube->unbind(sEnvironment);

            glActiveTexture(GL_TEXTURE0);
            glEnable(GL_TEXTURE_2D);
            glDisable(GL_CULL_FACE);
            glColor3f(1, 1, 1);
            for (int j = 0; j < 6; j++) {
                glPushMatrix();
                glTranslatef(lara->pos.x, lara->pos.y - 1024, lara->pos.z);
                switch (j) {
                    case 0 : glRotatef( 90, 0, 1, 0); break;
                    case 1 : glRotatef(-90, 0, 1, 0); break;
                    case 2 : glRotatef(-90, 1, 0, 0); break;
                    case 3 : glRotatef( 90, 1, 0, 0); break;
                    case 4 : glRotatef(  0, 0, 1, 0); break;
                    case 5 : glRotatef(180, 0, 1, 0); break;
                }
                glTranslatef(0, 0, 256);
                
                ambientCache->textures[j * 4 + dbg_ambient]->bind(sDiffuse);
                glBegin(GL_QUADS);
                    glTexCoord2f(0, 0); glVertex3f(-256,  256, 0);
                    glTexCoord2f(1, 0); glVertex3f( 256,  256, 0);
                    glTexCoord2f(1, 1); glVertex3f( 256, -256, 0);
                    glTexCoord2f(0, 1); glVertex3f(-256, -256, 0);
                glEnd();
                glPopMatrix();
            }
            glEnable(GL_CULL_FACE);
            glDisable(GL_TEXTURE_2D);


            glLineWidth(4);
            glBegin(GL_LINES);
            float S = 64.0f;
            for (int i = 0; i < level.roomsCount; i++) {
                TR::Room &r = level.rooms[i];                
                for (int j = 0; j < r.xSectors * r.zSectors; j++) {
                    TR::Room::Sector &s = r.sectors[j];
                    vec3 p = vec3(float((j / r.zSectors) * 1024 + 512 + r.info.x),
                                  float(max((s.floor - 2) * 256, (s.floor + s.ceiling) * 256 / 2)),
                                  float((j % r.zSectors) * 1024 + 512 + r.info.z));

                    AmbientCache::Cube &cube = ambientCache->items[ambientCache->offsets[i] + j];
                    if (cube.status == AmbientCache::Cube::READY) {
                        glColor3fv((GLfloat*)&cube.colors[0]);
                        glVertex3f(p.x + 0, p.y + 0, p.z + 0);
                        glVertex3f(p.x + S, p.y + 0, p.z + 0);

                        glColor3fv((GLfloat*)&cube.colors[1]);
                        glVertex3f(p.x + 0, p.y + 0, p.z + 0);
                        glVertex3f(p.x - S, p.y + 0, p.z + 0);

                        glColor3fv((GLfloat*)&cube.colors[2]);
                        glVertex3f(p.x + 0, p.y + 0, p.z + 0);
                        glVertex3f(p.x + 0, p.y + S, p.z + 0);

                        glColor3fv((GLfloat*)&cube.colors[3]);
                        glVertex3f(p.x + 0, p.y + 0, p.z + 0);
                        glVertex3f(p.x + 0, p.y - S, p.z + 0);

                        glColor3fv((GLfloat*)&cube.colors[4]);
                        glVertex3f(p.x + 0, p.y + 0, p.z + 0);
                        glVertex3f(p.x + 0, p.y + 0, p.z + S);

                        glColor3fv((GLfloat*)&cube.colors[5]);
                        glVertex3f(p.x + 0, p.y + 0, p.z + 0);
                        glVertex3f(p.x + 0, p.y + 0, p.z - S);
                    }
                }
            }
            glEnd();
            glLineWidth(1);           
        */

            Debug::Level::info(level, lara->getEntity(), lara->animation);


        Debug::end();
    }
    #endif

    void renderGame() {
        Core::invalidateTarget(true, true);
        params->clipHeight  = NO_CLIP_PLANE;
        params->clipSign    = 1.0f;
        params->waterHeight = params->clipHeight;

        if (ambientCache)
            ambientCache->processQueue();
        if (shadow)
            renderShadows(lara->getRoomIndex());

        if (shadow) shadow->bind(sShadow);
        Core::pass = Core::passCompose;

        setup();
        renderView(camera->getRoomIndex(), true);
    }

    void renderInventory() {
        Core::setTarget(NULL, true);
        inventory.render();
    }

    void renderUI() {
        if (level.isCutsceneLevel()) return;

        UI::begin();

        if (level.id != TR::TITLE) {
        // render health & oxygen bars
            vec2 size = vec2(180, 10);

            float health = lara->health / float(LARA_MAX_HEALTH);
            float oxygen = lara->oxygen / float(LARA_MAX_OXYGEN);

            if ((params->time - int(params->time)) < 0.5f) { // blinking
                if (health <= 0.2f) health = 0.0f;
                if (oxygen <= 0.2f) oxygen = 0.0f;
            }

            if (inventory.showHealthBar() || (!inventory.active && (!lara->emptyHands() || lara->damageTime > 0.0f || health <= 0.2f))) {
                UI::renderBar(UI::BAR_HEALTH, vec2(UI::width - 32 - size.x, 32), size, health);

                if (!inventory.active && !lara->emptyHands()) { // ammo
                    int index = inventory.contains(lara->getCurrentWeaponInv());
                    if (index > -1)
                        inventory.renderItemCount(inventory.items[index], vec2(UI::width - 32 - size.x, 64), size.x);
                }
            }

            if (!lara->dozy && (lara->stand == Lara::STAND_ONWATER || lara->stand == Character::STAND_UNDERWATER))
                UI::renderBar(UI::BAR_OXYGEN, vec2(32, 32), size, oxygen);
        }

        inventory.renderUI();

        if (level.id != TR::TITLE)
            UI::renderHelp();

        UI::end();
    }

    void render() {
        bool title  = inventory.isActive() || level.id == TR::TITLE;
        bool copyBg = title && lastTitle != title;
        lastTitle = title;

        if (isEnded) {
            Core::setTarget(NULL, true);
            UI::begin();
            UI::textOut(vec2(0, 480 - 16), STR_LOADING, UI::aCenter, UI::width);
            UI::end();
            return;
        }

        if (copyBg) {
            Core::defaultTarget = inventory.background[0];
            renderGame();
            Core::defaultTarget = NULL;

            inventory.prepareBackground();
        }

        if (!title)
            renderGame();

        if (title)
            renderInventory();

        renderUI();
    }

};

#endif