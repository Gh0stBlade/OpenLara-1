#ifndef H_TRIGGER
#define H_TRIGGER

#include "core.h"
#include "controller.h"
#include "character.h"
#include "sprite.h"

struct Switch : Controller {
    enum {
        STATE_DOWN,
        STATE_UP,
    };

    Switch(IGame *game, int entity) : Controller(game, entity) {}
    
    bool setTimer(float t) {
        if (activeState == asInactive) {
            if (state == STATE_DOWN && t > 0.0f) {
                timer = t;
                activeState = asActive;
            } else
                deactivate(true);
           return true;
        }
        return false;
    }
    
    virtual bool activate() {
        if (Controller::activate()) {
            animation.setState(state == STATE_UP ? STATE_DOWN : STATE_UP);
            return true;
        }
        return false;
    }

    virtual void update() {
        updateAnimation(true);
        getEntity().flags.active = TR::ACTIVE;
        if (!isActive())
            animation.setState(STATE_UP);
    }
};

struct Gear : Controller {
    enum {
        STATE_STATIC,
        STATE_ROTATE,
    };

    Gear(IGame *game, int entity) : Controller(game, entity) {}

    virtual void update() {
        updateAnimation(true);
        animation.setState(isActive() ? STATE_ROTATE : STATE_STATIC);
    }
};

struct Dart : Controller {
    vec3 velocity;
    vec3 dir;
    bool inWall;    // dart starts from wall
    bool armed;

    Dart(IGame *game, int entity) : Controller(game, entity), inWall(true), armed(true) {
        dir = vec3(sinf(angle.y), 0, cosf(angle.y));
    }

    virtual void update() {
        velocity = dir * animation.getSpeed();
        pos = pos + velocity * (Core::deltaTime * 30.0f);
        updateEntity();

        Controller *lara = (Controller*)level->laraController;
        if (armed && collide(lara)) {
            Sprite::add(game, TR::Entity::BLOOD, getRoomIndex(), (int)pos.x, (int)pos.y, (int)pos.z, Sprite::FRAME_ANIMATED);
            lara->hit(50.0f, this);
            armed = false;
        }

        TR::Level::FloorInfo info;
        level->getFloorInfo(getRoomIndex(), (int)pos.x, (int)pos.y, (int)pos.z, info);
        if (pos.y > info.floor || pos.y < info.ceiling || !insideRoom(pos, getRoomIndex())) {
            if (!inWall) {
                TR::Entity &e = getEntity();
                
                vec3 p = pos - dir * 64.0f; // wall offset = 64
                Sprite::add(game, TR::Entity::RICOCHET, e.room, (int)p.x, (int)p.y, (int)p.z, Sprite::FRAME_RANDOM);

                level->entityRemove(entity);
                delete this;
            }
        } else
            inWall = false;
    }
};

struct TrapDartgun : Controller {
    enum {
        STATE_IDLE,
        STATE_FIRE
    };

    TrapDartgun(IGame *game, int entity) : Controller(game, entity) {}
    
    virtual bool activate() {
        if (!Controller::activate())
            return false;
        
        animation.setState(STATE_FIRE);

        // add dart (bullet)
        TR::Entity &entity = getEntity();

        vec3 p = pos + vec3(0.0f, -512.0f, 256.0f).rotateY(PI - entity.rotation);

        int dartIndex = level->entityAdd(TR::Entity::TRAP_DART, entity.room, (int)p.x, (int)p.y, (int)p.z, entity.rotation, entity.intensity);
        if (dartIndex > -1) {
            Dart *dart = new Dart(game, dartIndex);
            dart->activate();
            level->entities[dartIndex].controller = dart; 
        }

        Sprite::add(game, TR::Entity::SMOKE, entity.room, (int)p.x, (int)p.y, (int)p.z);

        playSound(TR::SND_DART, pos, Sound::Flags::PAN);

        return true;
    }

    void virtual update() {
        updateAnimation(true);
        if (animation.canSetState(STATE_IDLE)) {
            animation.setState(STATE_IDLE);
            deactivate();
        }
    }
};

#define BOULDER_DAMAGE_GROUND 1000
#define BOULDER_DAMAGE_AIR    100

struct TrapBoulder : Controller {
    enum {
        STATE_FALL,
        STATE_ROLL,
    };

    vec3 velocity;

    TrapBoulder(IGame *game, int entity) : Controller(game, entity), velocity(0) {}

    virtual void update() {
        if (activeState != asActive) return;

        TR::Level::FloorInfo info;
        level->getFloorInfo(getRoomIndex(), int(pos.x), int(pos.y), int(pos.z), info);

        vec3 dir = getDir();

        if (pos.y >= info.floor - 256) {
            pos.y = float(info.floor);
            velocity = dir * animation.getSpeed();
            if (state != STATE_ROLL)
                animation.setState(STATE_ROLL);
        } else {
            if (velocity.y == 0.0f)
                velocity.y = 10.0f;
            velocity.y += GRAVITY * Core::deltaTime;
            animation.setState(STATE_FALL);
        }

        vec3 p = pos;
        pos += velocity * (30.0f * Core::deltaTime);

        if (info.roomNext != TR::NO_ROOM)
            getEntity().room = info.roomNext;

        game->checkTrigger(this, true);

        vec3 v = pos + getDir() * 512.0f;
        level->getFloorInfo(getRoomIndex(), int(v.x), int(v.y), int(v.z), info);
        if (pos.y > info.floor) {
            pos = p;
            deactivate();
            return;
        }

        Character *lara = (Character*)level->laraController;
        if (lara->health > 0.0f && collide(lara)) {
            if (lara->stand == Character::STAND_GROUND)
                lara->hit(BOULDER_DAMAGE_GROUND, this, TR::HIT_BOULDER);
            if (lara->stand == Character::STAND_AIR)
                lara->hit(BOULDER_DAMAGE_AIR * 30.0f * Core::deltaTime, this);
        }

        updateAnimation(true);
        updateEntity();
    }
};

// not a trigger
struct Block : Controller {
    enum {
        STATE_STAND = 1,
        STATE_PUSH,
        STATE_PULL,
    };

    Block(IGame *game, int entity) : Controller(game, entity) {
        updateFloor(true);
    }

    void updateFloor(bool rise) {
        TR::Entity &e = getEntity();
        TR::Level::FloorInfo info;
        level->getFloorInfo(e.room, e.x, e.y, e.z, info);
        if (info.roomNext != 0xFF)
            e.room = info.roomNext;
        int dx, dz;
        TR::Room::Sector &s = level->getSector(e.room, e.x, e.z, dx, dz);
        s.floor += rise ? -4 : 4;
    }

    bool doMove(bool push) {
    // check floor height of next floor
        vec3 dir = getDir() * (push ? 1024.0f : -1024.0f);
        TR::Entity &e = getEntity();
        TR::Level::FloorInfo info;

        int px = e.x + (int)dir.x;
        int pz = e.z + (int)dir.z;
        level->getFloorInfo(e.room, px, e.y, pz, info);

        if ((info.slantX | info.slantZ) || info.floor != e.y || info.floor - info.ceiling < 1024)
            return false;

        // check for trapdoor
        px /= 1024;
        pz /= 1024;
        for (int i = 0; i < info.trigCmdCount; i++)
            if (info.trigCmd[i].action == TR::Action::ACTIVATE) {
                TR::Entity &obj = level->entities[info.trigCmd[i].args];
                if ((obj.type == TR::Entity::TRAP_DOOR_1 || obj.type == TR::Entity::TRAP_DOOR_2) && px == obj.x / 1024 && pz == obj.z / 1024)
                    return false;
            }

        // check Laras destination position
        if (!push) {
            dir = getDir() * (-2048.0f);
            px = e.x + (int)dir.x;
            pz = e.z + (int)dir.z;
            level->getFloorInfo(e.room, px, e.y, pz, info);
            if ((info.slantX | info.slantZ) || info.floor != e.y || info.floor - info.ceiling < 1024)
                return false;
        }

        if (!animation.setState(push ? STATE_PUSH : STATE_PULL))
            return false;
        updateFloor(false);
        activate();
        return true;
    }

    virtual void update() {
        if (state == STATE_STAND) return;
        updateAnimation(true);
        if (state == STATE_STAND) {
            updateEntity();
            updateFloor(true);
            deactivate();
            game->checkTrigger(this, true);
        }
        updateLights();
    }
};


struct MovingBlock : Controller {
    enum {
        STATE_BEGIN,
        STATE_END,
        STATE_MOVE,
    };

    MovingBlock(IGame *game, int entity) : Controller(game, entity) {
        if (!getEntity().flags.invisible)
            updateFloor(true);
    }

    void updateFloor(bool rise) {
        updateEntity();
        TR::Entity &e = getEntity();
        TR::Level::FloorInfo info;
        level->getFloorInfo(e.room, e.x, e.y, e.z, info);
        if (info.roomNext != 0xFF)
            e.room = info.roomNext;
        int dx, dz;
        TR::Room::Sector &s = level->getSector(e.room, e.x, e.z, dx, dz);
        s.floor += rise ? -8 : 8;
    }

    virtual void update() {
        updateAnimation(true);

        if (isActive()) {
            if (state == STATE_BEGIN) {
                updateFloor(false);
                animation.setState(STATE_END);
            }
        } else {
            if (state == STATE_END) {
                updateFloor(false);
                animation.setState(STATE_BEGIN);
            }
        }

        if (activeState == asInactive) {
            if (getEntity().flags.active == TR::ACTIVE)
                activeState = asActive; // stay in active items list
            pos.x = int(pos.x / 1024.0f) * 1024.0f + 512.0f;
            pos.z = int(pos.z / 1024.0f) * 1024.0f + 512.0f;
            updateFloor(true);
            game->checkTrigger(this, true);
            return;
        }

        pos += getDir() * (animation.getSpeed() * Core::deltaTime * 30.0f);
    }
};


struct Door : Controller {
    enum {
        STATE_CLOSE,
        STATE_OPEN,
    };

    struct BlockInfo {
        int              roomIndex[2];
        int              sectorIndex[2];
        TR::Room::Sector sectors[2];

        BlockInfo() {}
        BlockInfo(TR::Level *level, int room, int nx, int nz, int x, int z, bool flip) {
        // front
            roomIndex[0] = room;
            roomIndex[1] = TR::NO_ROOM;

            if (roomIndex[0] == TR::NO_ROOM)
                return;
            if (flip && level->rooms[roomIndex[0]].alternateRoom != -1)
                roomIndex[0] = level->rooms[roomIndex[0]].alternateRoom;

            sectors[0] = level->getSector(roomIndex[0], x, z, sectorIndex[0]);

        // behind
            roomIndex[1] = level->getNextRoom(sectors[0].floorIndex);

            if (roomIndex[1] == TR::NO_ROOM)
                return;
            if (flip && level->rooms[roomIndex[1]].alternateRoom != -1)
                roomIndex[1] = level->rooms[roomIndex[1]].alternateRoom;

            sectors[1] = level->getSector(roomIndex[1], nx, nz, sectorIndex[1]);
        }

        void set(TR::Level *level) {
            for (int i = 0; i < 2; i++)
                if (roomIndex[i] != TR::NO_ROOM) {
                    TR::Room::Sector &s = level->rooms[roomIndex[i]].sectors[sectorIndex[i]];
                    s.floorIndex = 0;
                    s.boxIndex   = TR::NO_BOX;
                    s.roomBelow  = TR::NO_ROOM;
                    s.floor      = TR::NO_FLOOR;
                    s.roomAbove  = TR::NO_ROOM;
                    s.ceiling    = TR::NO_FLOOR;

                    if (sectors[i].boxIndex != TR::NO_BOX) {
                        ASSERT(sectors[i].boxIndex < level->boxesCount);
                        TR::Box &box = level->boxes[sectors[i].boxIndex];
                        if (box.overlap.blockable)
                            box.overlap.block = true;
                    }
                }
        }

        void reset(TR::Level *level) {
            for (int i = 0; i < 2; i++)
                if (roomIndex[i] != TR::NO_ROOM) {
                    level->rooms[roomIndex[i]].sectors[sectorIndex[i]] = sectors[i];
                    if (sectors[i].boxIndex != TR::NO_BOX) {
                        TR::Box &box = level->boxes[sectors[i].boxIndex];
                        if (box.overlap.blockable)
                            box.overlap.block = false;
                    }
                }
        }

    } block[2];

    Door(IGame *game, int entity) : Controller(game, entity) {
        TR::Entity &e = getEntity();
        vec3 p = pos - getDir() * 1024.0f;
        block[0] = BlockInfo(level, e.room, e.x, e.z, int(p.x), int(p.z), false);
        block[1] = BlockInfo(level, e.room, e.x, e.z, int(p.x), int(p.z), true);
        updateBlock(false);
    }

    void updateBlock(bool open) {
        if (open) {
            block[0].reset(level);
            block[1].reset(level);
        } else {
            block[0].set(level);
            block[1].set(level);
        }
    }
    
    virtual void update() {
        updateAnimation(true);
        int targetState = isActive() ? STATE_OPEN : STATE_CLOSE;

        if (state == targetState)
            updateBlock(targetState == STATE_OPEN);
        else
            animation.setState(targetState);
    }
};

struct TrapDoor : Controller {
    enum {
        STATE_CLOSE,
        STATE_OPEN,
    };

    TrapDoor(IGame *game, int entity) : Controller(game, entity) {
        getEntity().flags.collision = true;
    }
    
    virtual void update() {
        updateAnimation(true);
        int targetState = isActive() ? STATE_OPEN : STATE_CLOSE;

        if (state == targetState)
            getEntity().flags.collision = targetState == STATE_CLOSE;
        else
            animation.setState(targetState);
    }
};


struct TrapFloor : Controller {
    enum {
        STATE_STATIC,
        STATE_SHAKE,
        STATE_FALL,
        STATE_DOWN,
    };
    float speed;

    TrapFloor(IGame *game, int entity) : Controller(game, entity), speed(0) {
        getEntity().flags.collision = true;
    }

    virtual bool activate() {
        if (state != STATE_STATIC) return false;
        TR::Entity &e = ((Controller*)level->laraController)->getEntity();
        int ey = getEntity().y - 512; // real floor object position
        if (abs(e.y - ey) <= 8 && Controller::activate()) {
            animation.setState(STATE_SHAKE);
            return true;
        }
        return false;
    }

    virtual void update() {
        updateAnimation(true);
        if (state == STATE_FALL) {
            getEntity().flags.collision = false;
            speed += GRAVITY * 30 * Core::deltaTime;
            pos.y += speed * Core::deltaTime;

            TR::Level::FloorInfo info;
            level->getFloorInfo(getRoomIndex(), int(pos.x), int(pos.y), int(pos.z), info);

            if (pos.y > info.roomFloor && info.roomBelow != 0xFF)
                getEntity().room = info.roomBelow;

            if (pos.y > info.floor) {
                pos.y = (float)info.floor;
                animation.setState(STATE_DOWN);
            }
            updateEntity();
        }
    }
};

struct Bridge : Controller {
    Bridge(IGame *game, int entity) : Controller(game, entity) {
        getEntity().flags.collision = true;
    }
};

struct Drawbridge : Controller {
    enum {
        STATE_UP,
        STATE_DOWN,
    };

    Drawbridge(IGame *game, int entity) : Controller(game, entity) {
        getEntity().flags.collision = true;
    }

    virtual void update() {
        updateAnimation(true);
        animation.setState(isActive() ? STATE_DOWN : STATE_UP);
    }
};

struct Crystal : Controller {
    Texture *environment;

    Crystal(IGame *game, int entity) : Controller(game, entity) {
        environment = new Texture(64, 64, Texture::RGBA, true);
        activate();
    }

    virtual ~Crystal() {
        delete environment;
    }

    virtual void update() {
        updateAnimation(false);
    }

    virtual void render(Frustum *frustum, MeshBuilder *mesh, Shader::Type type, bool caustics) {
        Shader *sh = Core::active.shader;
        sh->setParam(uMaterial, vec4(1.0f));
        environment->bind(sEnvironment);
        Controller::render(frustum, mesh, type, caustics);
    }
};

#define BLADE_DAMAGE    100
#define BLADE_RANGE     1024

struct TrapBlade : Controller {
    enum {
        STATE_STATIC = 0,
        STATE_SWING  = 2,
    };

    TrapBlade(IGame *game, int entity) : Controller(game, entity) {}

    virtual void update() {
        updateAnimation(true);

        if (isActive()) {
            if (state == STATE_STATIC)
                animation.setState(STATE_SWING);
        } else {
            if (state == STATE_SWING)
                animation.setState(STATE_STATIC);
        }

        if (state != STATE_SWING)
            return;

        int f = animation.frameIndex;
        if ((f <= 8 || f >= 20) && (f <= 42 || f >= 57))
            return;

        Character* lara = (Character*)level->laraController;
        if (!checkRange(lara, BLADE_RANGE) || !collide(lara))
            return;

        lara->hit(BLADE_DAMAGE * 30.0f * Core::deltaTime, this, TR::HIT_BLADE);
    }
};

#define SPIKES_DAMAGE_FALL      1000
#define SPIKES_DAMAGE_RUN       15
#define SPIKES_RANGE            1024

struct TrapSpikes : Controller {
    TrapSpikes(IGame *game, int entity) : Controller(game, entity) {
        activate();
    }

    virtual void update() {
        Character *lara = (Character*)level->laraController;
        if (lara->health <= 0.0f) return;

        if (!checkRange(lara, SPIKES_RANGE) || !collide(lara))
            return;

        if (lara->stand != Character::STAND_AIR || lara->velocity.y <= 0.0f || (pos.y - lara->pos.y) > 256.0f) {
            if (lara->speed < 30.0f)
                return;
            lara->hit(SPIKES_DAMAGE_RUN * 30.0f * Core::deltaTime, this, TR::HIT_SPIKES);
        } else
            lara->hit(SPIKES_DAMAGE_FALL, this, TR::HIT_SPIKES);
    }
};

struct TrapCeiling : Controller {
    enum {
        STATE_STATIC,
        STATE_FALL,
        STATE_DOWN,
    };

    float speed;

    TrapCeiling(IGame *game, int entity) : Controller(game, entity), speed(0) {}

    virtual void update() {
        updateAnimation(true);

        if (state == STATE_STATIC)
            animation.setState(STATE_FALL);
       
        if (state == STATE_FALL) {
            speed += GRAVITY * 30 * Core::deltaTime;
            pos.y += speed * Core::deltaTime;

            TR::Level::FloorInfo info;
            level->getFloorInfo(getRoomIndex(), int(pos.x), int(pos.y), int(pos.z), info);

            if (pos.y > info.roomFloor && info.roomBelow != 0xFF)
                getEntity().room = info.roomBelow;

            if (pos.y > info.floor) {
                pos.y = (float)info.floor;
                animation.setState(STATE_DOWN);
            }
            updateEntity();

            Controller *lara = (Controller*)level->laraController;
            if (collide(lara))
                lara->hit(1000);
        }
    }
};

#define SLAM_DAMAGE 400

struct TrapSlam : Controller {
    enum {
        STATE_OPEN,
        STATE_SLAM,
    };
    
    bool bite;

    TrapSlam(IGame *game, int entity) : Controller(game, entity), bite(false) {}

    virtual void update() {
        if (isActive()) {
            animation.setState(STATE_SLAM);

            if (animation.frameIndex >= 20)
                bite = false;

            Character *lara = (Character*)level->laraController;
            if (animation.state == STATE_SLAM && !bite && collide(lara)) {
                lara->hit(SLAM_DAMAGE, this, TR::HIT_SLAM);
                bite = true;
            }

        } else
            animation.setState(STATE_OPEN);

        updateAnimation(true);
    }
};


struct TrapSword : Controller {
    TrapSword(IGame *game, int entity) : Controller(game, entity) {}

    virtual void update() {
        updateAnimation(true);
    }
};


struct TrapLava : Controller {
    bool done;

    TrapLava(IGame *game, int entity) : Controller(game, entity), done(false) {}

    virtual void update() {
        Character *lara = (Character*)level->laraController;
        if (lara->health > 0.0f && collide(lara))
            lara->hit(1000.0f, this, TR::HIT_FLAME);

        if (done) {
            deactivate();
            return;
        }

        vec3 dir = getDir();
        pos += dir * (25.0f * 30.0f * Core::deltaTime);

        updateEntity();
        int roomIndex = getRoomIndex();
        TR::Room::Sector *s = level->getSector(roomIndex, int(pos.x + dir.x * 2048.0f), int(pos.y), int(pos.z + dir.z * 2048.0f));
        if (!s || s->floor * 256 != int(pos.y))
            done = true;
        getEntity().room = roomIndex;
    }
};


struct DoorLatch : Controller {
    enum {
        STATE_CLOSE,
        STATE_OPEN,
    };

    DoorLatch(IGame *game, int entity) : Controller(game, entity) {
        getEntity().flags.collision = true;
    }

    virtual void update() {
        updateAnimation(true);
        animation.setState(isActive() ? STATE_OPEN : STATE_CLOSE);
    }
};


struct Cabin : Controller {
    enum {
        STATE_UP,
        STATE_DOWN_1,
        STATE_DOWN_2,
        STATE_DOWN_3,
        STATE_GROUND,
    };

    Cabin(IGame *game, int entity) : Controller(game, entity) {}

    virtual void update() {
        TR::Entity &e = getEntity();

        if (e.flags.active == TR::ACTIVE) {
            if (state >= STATE_UP && state <= STATE_DOWN_2)
                animation.setState(state + 1);
            e.flags.active = 0;
        }

        if (state == STATE_GROUND) {
            e.flags.invisible        = true;
            level->flipmap[3].active = TR::ACTIVE;
            level->isFlipped         = !level->isFlipped;
            deactivate(true);
        }

        updateAnimation(true);
    }
};

struct KeyHole : Controller {
    KeyHole(IGame *game, int entity) : Controller(game, entity) {}

    virtual bool activate() {
        if (!Controller::activate()) return false;
        getEntity().flags.active = TR::ACTIVE;
        if (getEntity().isPuzzleHole()) {
            int doneIdx = TR::Entity::convToInv(TR::Entity::getItemForHole(getEntity().type)) - TR::Entity::INV_PUZZLE_1;
            meshSwap(0, level->extra.puzzleDone[doneIdx]);
        }
        deactivate();
        return true;
    }

    virtual void update() {}
};


struct Waterfall : Controller {
    #define SPLASH_TIMESTEP (1.0f / 30.0f)

    float timer;

    Waterfall(IGame *game, int entity) : Controller(game, entity), timer(0.0f) {}

    virtual void update() {
        updateAnimation(true);

        vec3 delta = (((ICamera*)level->cameraController)->pos - pos) * (1.0f / 1024.0f);
        if (delta.length2() > 100.0f)
            return;

        timer -= Core::deltaTime;
        if (timer > 0.0f) return;
        timer += SPLASH_TIMESTEP * (1.0f + randf() * 0.25f);

        float dropRadius   = randf() * 128.0f + 128.0f;
        float dropStrength = randf() * 0.1f + 0.05f;

        vec2 p = (vec2(randf(), randf()) * 2.0f - 1.0f) * (512.0f - dropRadius);
        vec3 dropPos = pos + vec3(p.x, 0.0f, p.y);
        game->waterDrop(dropPos, dropRadius, dropStrength);

        Sprite::add(game, TR::Entity::WATER_SPLASH, getRoomIndex(), (int)dropPos.x, (int)dropPos.y, (int)dropPos.z);
    } 

    #undef SPLASH_TIMESTEP
};

struct Bubble : Sprite {
    float speed;

    Bubble(IGame *game, int entity) : Sprite(game, entity, true, Sprite::FRAME_RANDOM) {
        speed = (10.0f + randf() * 6.0f) * 30.0f;
    // get water height => bubble life time
        TR::Entity &e = getEntity();
        int dx, dz;
        int room = getRoomIndex();
        int h = e.y;
        while (room != TR::NO_ROOM && level->rooms[room].flags.water) {
            TR::Room::Sector &s = level->getSector(room, e.x, e.z, dx, dz);
            h = s.ceiling * 256;
            room = s.roomAbove;
        }
        time -= (e.y - h) / speed - (1.0f / SPRITE_FPS);
        activate();
    }

    virtual ~Bubble() {
        game->waterDrop(pos, 64.0f, 0.01f);
    }

    virtual void update() {
        pos.y -= speed * Core::deltaTime;
        angle.x += 30.0f * 13.0f * DEG2RAD * Core::deltaTime;
        angle.y += 30.0f *  9.0f * DEG2RAD * Core::deltaTime;
        pos.x += sinf(angle.y) * (11.0f * 30.0f * Core::deltaTime);
        pos.z += cosf(angle.x) * (8.0f  * 30.0f * Core::deltaTime);
        updateEntity();
        Sprite::update();
    }
};

#endif