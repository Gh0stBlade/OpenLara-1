#ifndef H_CAMERA
#define H_CAMERA

#include "core.h"
#include "frustum.h"
#include "controller.h"
#include "character.h"

#define CAM_OFFSET_FOLLOW (1024.0f + 512.0f)
#define CAM_OFFSET_COMBAT (2048.0f + 512.0f)

#define CAM_SPEED_FOLLOW  12
#define CAM_SPEED_COMBAT  8

struct Camera : ICamera {
    IGame      *game;
    TR::Level  *level;
    Character  *owner;
    Frustum    *frustum;

    float       fov, aspect, znear, zfar;
    vec3        advAngle, targetAngle;
    float       advTimer;
    mat4        mViewInv;

    float       timer;

    int         viewIndex;
    int         viewIndexLast;
    Controller* viewTarget;
    Basis       fpHead;
    int         speed;
    bool        smooth;

    Camera(IGame *game, Character *owner) : ICamera(), game(game), level(game->getLevel()), frustum(new Frustum()), timer(-1.0f), viewIndex(-1), viewIndexLast(-1), viewTarget(NULL) {
        this->owner = owner;
        changeView(false);
        if (level->isCutsceneLevel()) {
            mode = MODE_CUTSCENE;
//            room  = level->entities[level->cutEntity].room;
            timer = 0.0f;
        } else
            mode = MODE_FOLLOW;

        eye.pos     = owner->pos;
        eye.room    = owner->getRoomIndex();
        target.pos  = owner->pos;
        target.room = eye.room;

        target.pos.y -= 1024;
        eye.pos.z  -= 100;

        advTimer = -1.0f;

        speed  = CAM_SPEED_FOLLOW;
        smooth = false;
    }

    virtual ~Camera() {
        delete frustum;
    }
    
    virtual Controller* getOwner() {
        return owner;
    }

    virtual int getRoomIndex() const {
        return (level->state.flags.flipped && level->rooms[eye.room].alternateRoom > -1) ? level->rooms[eye.room].alternateRoom : eye.room;
    }

    virtual void checkRoom() {
    //    level->getSector(eye.room, eye.pos);
    //    return;

        if (mode == MODE_CUTSCENE) {
            for (int i = 0; i < level->roomsCount; i++)
                if (owner->insideRoom(eye.pos, i)) {
                    eye.room = i;
                    break;
                }
            return;
        }

        TR::Level::FloorInfo info;
        owner->getFloorInfo(getRoomIndex(), eye.pos, info);

        if (info.roomNext != TR::NO_ROOM)
            eye.room = info.roomNext;
        
        if (eye.pos.y < info.roomCeiling) {
            if (info.roomAbove != TR::NO_ROOM)
                eye.room = info.roomAbove;
            else
                if (info.roomCeiling != 0xffff8100)
                    eye.pos.y = (float)info.roomCeiling;
        }

        if (eye.pos.y > info.roomFloor) {
            if (info.roomBelow != TR::NO_ROOM)
                eye.room = info.roomBelow;
            else
                if (info.roomFloor != 0xffff8100)
                    eye.pos.y = (float)info.roomFloor;
        }
    }

    void updateListener() {
        Sound::flipped = level->state.flags.flipped;
        Sound::listener[cameraIndex].matrix = mViewInv;
        if (cameraIndex == 0) { // reverb effect only for main player
            TR::Room &r = level->rooms[getRoomIndex()];
            int h = (r.info.yBottom - r.info.yTop) / 1024;
            Sound::reverb.setRoomSize(vec3(float(r.xSectors), float(h), float(r.zSectors)) * 2.419f); // convert cells size into meters
        }
    }

    bool isUnderwater() {
        return level->rooms[getRoomIndex()].flags.water;
    }

    vec3 getViewPoint() {
        Box box = owner->getBoundingBox();
        vec3 pos = owner->pos;
        vec3 center = box.center();

        if (centerView) {
            pos.x = center.x;
            pos.z = center.z;
        }

        if (mode != MODE_STATIC)
            pos.y = box.max.y + (box.min.y - box.max.y) * (3.0f / 4.0f);
        else
            pos.y = center.y;

        if (owner->stand != Character::STAND_UNDERWATER)
            pos.y -= 256;

        return pos;
    }

    void setView(int viewIndex, float timer, int speed) {
        viewIndexLast   = viewIndex;
        smooth          = speed > 0;
        mode            = MODE_STATIC;
        this->viewIndex = viewIndex;
        this->timer     = timer;
        this->speed     = speed * 8;
    }

    void resetTarget() {
        smooth        = speed > 0;
        mode          = MODE_FOLLOW;
        viewIndex     = -1;
        viewTarget    = NULL;
        timer         = -1.0f;
        speed         = CAM_SPEED_FOLLOW;
    }

    virtual void doCutscene(const vec3 &pos, float rotation) {
        mode = Camera::MODE_CUTSCENE;
        level->cutMatrix.identity();
        level->cutMatrix.rotateY(rotation);
        level->cutMatrix.setPos(pos);
        timer = 0.0f;
    }

    bool updateFirstPerson() {
        Basis &joint = owner->getJoint(owner->jointHead);

        if (mode != MODE_CUTSCENE && !owner->useHeadAnimation()) {
            targetAngle.x += PI;
            targetAngle.z = -targetAngle.z;

            vec3 pos = joint.pos - joint.rot * vec3(0, 48, -24);
            quat rot = rotYXZ(targetAngle);

            fpHead.pos = pos;
            fpHead.rot = fpHead.rot.lerp(rot, smooth ? Core::deltaTime * 10.0f : 1.0f);
        } else {
            fpHead = joint;
            fpHead.rot = fpHead.rot * quat(vec3(1, 0, 0), PI);
            fpHead.pos -= joint.rot * vec3(0, 48, -24);
        }

        mViewInv.identity();
        mViewInv.setRot(fpHead.rot);
        mViewInv.setPos(fpHead.pos);

        eye.pos  = mViewInv.getPos();
        eye.room = owner->getRoomIndex();

        level->getSector(eye.room, fpHead.pos);
        return true;
    }

    void clipSlide(float offset, float *toX, float *toZ, float targetX, float targetZ, const TR::Box &box) {
        TR::Box sqBox;
        sqBox.minZ = SQR(int(targetZ) - box.minZ);
        sqBox.maxZ = SQR(int(targetZ) - box.maxZ);
        sqBox.minX = SQR(int(targetX) - box.minX);
        sqBox.maxX = 0; // unused

        offset *= offset;

        int32 h = sqBox.minX + sqBox.minZ;
        if (h > 256 * 256) {
            *toX = float(box.minX);
            if (offset > h || offset < sqBox.minX)
                *toZ = float(box.minZ);
            else
                *toZ = targetZ + sqrtf(offset - sqBox.minX) * sign(box.minZ - box.maxZ);
            return;
        }
    
        h = sqBox.minX + sqBox.maxZ;
        if (h > 256 * 256) {
            *toX = float(box.minX);
            if (offset > h || offset < sqBox.minX)
                *toZ = float(box.maxZ);
            else
                *toZ = targetZ - sqrtf(offset - sqBox.minX) * sign(box.minZ - box.maxZ);
            return;
        }

        h = sqBox.maxX + sqBox.minZ;
        *toZ = float(box.minZ);
        if (offset > h || offset < sqBox.minZ)
            *toX = float(box.maxX);
        else
            *toX = targetX - sqrtf(offset - sqBox.minZ) * sign(box.minX - box.maxX);
    }

    void traceClip(float offset, TR::Location &to) {
        trace(target, to);

        uint16 ownerBoxIndex  = level->getSector(target.room, target.pos)->boxIndex;
        uint16 cameraBoxIndex = level->getSector(to.room, to.pos)->boxIndex;

        TR::Box cBox = level->boxes[(cameraBoxIndex != TR::NO_BOX && !level->boxes[ownerBoxIndex].contains(int(to.pos.x), int(to.pos.z))) ? cameraBoxIndex : ownerBoxIndex];

        clipBox(to.room, to.pos, cBox);
        cBox.expand(-256);

        // transform coord system by clipping direction
        float *toX    = &to.pos.x, 
              *toZ    = &to.pos.z,
              targetX = target.pos.x, 
              targetZ = target.pos.z;

        if (to.pos.z < cBox.minZ || to.pos.z > cBox.maxZ) {
            swap(toX, toZ);
            swap(targetX, targetZ);
            swap(cBox.minX, cBox.minZ);
            swap(cBox.maxX, cBox.maxZ);
        }

        if (*toX >= cBox.minX && *toX <= cBox.maxX)
            return;

        if (*toX > cBox.maxX)
            swap(cBox.minX, cBox.maxX);
    
        if (*toZ > targetZ)
            swap(cBox.minZ, cBox.maxZ);

        clipSlide(offset, toX, toZ, targetX, targetZ, cBox);
        level->getSector(to.room, to.pos);
    }

    int traceX(const TR::Location &from, TR::Location &to) {
        vec3 d = to.pos - from.pos;
        if (fabsf(d.x) < EPS) return 1;

        d.yz() *= 1024 / d.x;

        vec3 p = from.pos;

        p.x = float(int(p.x) / 1024 * 1024);
        if (d.x > 0) p.x += 1023;

        p.yz() += d.yz() * ((p.x - from.pos.x) / 1024);

        float s = float(sign(d.x));
        d.x = 1024;
        d *= s;

        int16 roomIndex = from.room;
        while ((p.x - to.pos.x) * s < 0) {
            if (level->isBlocked(roomIndex, p)) {
                to.pos  = p;
                to.room = roomIndex;
                return -1;
            }

            to.room = roomIndex;
            if (level->isBlocked(roomIndex, vec3(p.x + s, p.y, p.z))) {
                to.pos = p;
                return 0;
            }

            p += d;
        }

        to.room = roomIndex;
        return 1;
    }

    int traceZ(const TR::Location &from, TR::Location &to) {
        vec3 d = to.pos - from.pos;
        if (fabsf(d.z) < EPS) return 1;

        d.xy() *= 1024 / d.z;

        vec3 p = from.pos;
        
        p.z = float(int(p.z) / 1024 * 1024);
        if (d.z > 0) p.z += 1023;

        p.xy() += d.xy() * ((p.z - from.pos.z) / 1024);

        float s = float(sign(d.z));
        d.z = 1024;
        d *= s;

        int16 roomIndex = from.room;
        while ((p.z - to.pos.z) * s < 0) {
            if (level->isBlocked(roomIndex, p)) {
                to.pos  = p;
                to.room = roomIndex;
                return -1;
            }

            to.room = roomIndex;
            if (level->isBlocked(roomIndex, vec3(p.x, p.y, p.z + s))) {
                to.pos = p;
                return 0;
            }

            p += d;
        }

        to.room = roomIndex;
        return 1;
    }

    bool trace(const TR::Location &from, TR::Location &to) {
        int rx, rz;

        if (fabsf(to.pos.x - from.pos.x) < fabsf(to.pos.z - from.pos.z)) {
            rz = traceZ(from, to);
            if (!rz) return false;
            rx = traceX(from, to);
        } else {
            rx = traceX(from, to);
            if (!rx) return false;
            rz = traceZ(from, to);
        }
        TR::Room::Sector *sector = level->getSector(to.room, to.pos);
        return clipHeight(from, to, sector) && rx == 1 && rz == 1;
    }

    bool clipHeight(const TR::Location &from, TR::Location &to, TR::Room::Sector *sector) {
        vec3 dir = to.pos - from.pos;

        float y = level->getFloor(sector, to.pos);
        if (to.pos.y <= y || from.pos.y >= y) {
            y = level->getCeiling(sector, to.pos);
            if (to.pos.y >= y || from.pos.y <= y)
                return true;
        }

        ASSERT(dir.y != 0.0f);

        float d = (y - from.pos.y) / dir.y;
        to.pos.y = y;
        to.pos.x = from.pos.x + dir.x * d;
        to.pos.z = from.pos.z + dir.z * d;
        return false;
    }

    void clipBox(int16 roomIndex, const vec3 &pos, TR::Box &box) {
        const TR::Room &room = level->rooms[roomIndex];
    
        int dx = int(pos.x) / 1024 * 1024;
        int dz = int(pos.z) / 1024 * 1024;

        TR::Box border;
        border.minX = dx - 1;
        border.minZ = dz - 1;
        border.maxX = dx + 1024;
        border.maxZ = dz + 1024;

        for (int i = 0; i < 4; i++) {
            vec3 p = pos;

            if (i / 2)
                p.x = float(border.sides[i]);
            else
                p.z = float(border.sides[i]);

            int16 index = roomIndex;
            if (level->isBlocked(index, p)) {
                box.sides[i] = border.sides[i];
                continue;
            }

            int sx = (int(p.x) - room.info.x) / 1024;
            int sz = (int(p.z) - room.info.z) / 1024;

            int boxIndex = room.sectors[sz + sx * room.zSectors].boxIndex;
        
            if (boxIndex != TR::NO_BOX) {
                if (i % 2)
                    box.sides[i] = max(box.sides[i], level->boxes[boxIndex].sides[i]);
                else
                    box.sides[i] = min(box.sides[i], level->boxes[boxIndex].sides[i]);
            }
        }
    }

    void move(const TR::Location &to) {
        float t = (smooth && speed) ? (30.0f / speed * Core::deltaTime) : 1.0f;

        eye.pos  = eye.pos.lerp(to.pos, t);
        eye.room = to.room;

        TR::Room::Sector *sector = level->getSector(eye.room, eye.pos);

        float floor = level->getFloor(sector, eye.pos) - 256;

        if (to.pos.y >= floor && eye.pos.y >= floor) {
            trace(target, eye);
            sector = level->getSector(eye.room, eye.pos);
            floor  = level->getFloor(sector, eye.pos) - 256;
        }
    
        float ceiling = level->getCeiling(sector, eye.pos) + 256;
        if (floor < ceiling)
            floor = ceiling = (floor + ceiling) * 0.5f;

        if (eye.pos.y > floor)   eye.pos.y = floor;
        if (eye.pos.y < ceiling) eye.pos.y = ceiling;

        level->getSector(eye.room, eye.pos);
    }

    virtual void update() {
        if (shake > 0.0f)
            shake = max(0.0f, shake - Core::deltaTime);

        if (mode == MODE_CUTSCENE) {
            timer += Core::deltaTime * 30.0f;
            float t = timer - int(timer);
            int indexA = min(int(timer), level->cameraFramesCount - 1);
            int indexB = min((indexA + 1), level->cameraFramesCount - 1);

            if (indexA == level->cameraFramesCount - 1) {
                if (level->isCutsceneLevel())
                    game->loadNextLevel();
                else {
                    Character *lara = (Character*)owner;
                    if (lara->health > 0.0f)
                        mode = MODE_FOLLOW;
                }
            }

            if (!firstPerson) {
                TR::CameraFrame *frameA = &level->cameraFrames[indexA];
                TR::CameraFrame *frameB = &level->cameraFrames[indexB];

                const int eps = 512;

                if (abs(frameA->pos.x - frameB->pos.x) > eps || abs(frameA->pos.y - frameB->pos.y) > eps || abs(frameA->pos.z - frameB->pos.z) > eps) {
                    eye.pos    = frameA->pos;
                    target.pos = frameA->target;
                    fov        = frameA->fov / 32767.0f * 120.0f;
                } else {
                    eye.pos    = vec3(frameA->pos).lerp(frameB->pos, t);
                    target.pos = vec3(frameA->target).lerp(frameB->target, t);
                    fov        = lerp(frameA->fov / 32767.0f * 120.0f, frameB->fov / 32767.0f * 120.0f, t);
                }

                eye.pos    = level->cutMatrix * eye.pos;
                target.pos = level->cutMatrix * target.pos;
            } else
                updateFirstPerson();

            mViewInv = mat4(eye.pos, target.pos, vec3(0, -1, 0));

            checkRoom();
        } else {
            Controller *lookAt = NULL;

            if (mode != MODE_STATIC) {
                if (!owner->viewTarget) {
                    if (viewTarget && !viewTarget->flags.invisible) {
                        vec3 targetVec = (viewTarget->pos - owner->pos).normal();
                        if (targetVec.dot(owner->getDir()) > 0.5f)
                            lookAt = viewTarget;
                    }
                } else
                    lookAt = owner->viewTarget;

                owner->lookAt(lookAt);
            } else {
                lookAt = viewTarget;
                owner->lookAt(NULL);
            }

            vec3 advAngleOld = advAngle;

            if (Input::down[ikMouseL]) {
                vec2 delta = Input::mouse.pos - Input::mouse.start.L;
                advAngle.x -= delta.y * 0.01f;
                advAngle.y += delta.x * 0.01f;
                Input::mouse.start.L = Input::mouse.pos;
            }

            // TODO: use player index
            advAngle.x -= Input::joy[cameraIndex].R.y * 2.0f * Core::deltaTime;
            advAngle.y += Input::joy[cameraIndex].R.x * 2.0f * Core::deltaTime;

            if (advAngleOld == advAngle) {
                if (advTimer > 0.0f) {
                    advTimer = max(0.0f, advTimer - Core::deltaTime);
                }
            } else
                advTimer = -1.0f;

            if (owner->velocity != 0.0f && advTimer < 0.0f && !Input::down[ikMouseL])
                advTimer = -advTimer;

            if (advTimer == 0.0f && advAngle != 0.0f) {
                float t = 10.0f * Core::deltaTime;
                advAngle.x = lerp(clampAngle(advAngle.x), 0.0f, t);
                advAngle.y = lerp(clampAngle(advAngle.y), 0.0f, t);
            }

            targetAngle = owner->angle + advAngle;
            if (mode == MODE_FOLLOW || mode == MODE_COMBAT)
                targetAngle += angle;

            targetAngle.x = clamp(targetAngle.x, -85 * DEG2RAD, +85 * DEG2RAD);

            if (!firstPerson || viewIndex != -1) {

                if (timer > 0.0f) {
                    timer -= Core::deltaTime;
                    if (timer <= 0.0f)
                        resetTarget();
                }

                TR::Location to;

                target.box  = TR::NO_BOX;
                if (viewIndex > -1) {
                    TR::Camera &cam = level->cameras[viewIndex];
                    to.room = cam.room;
                    to.pos  = vec3(float(cam.x), float(cam.y), float(cam.z));
                    if (lookAt) {
                        target.room = lookAt->getRoomIndex();
                        target.pos  = lookAt->getBoundingBox().center();
                    } else {
                        target.room = owner->getRoomIndex();
                        target.pos  = owner->pos;
                        target.pos.y -= 512.0f;
                    }
                } else {
                    vec3 p = getViewPoint();
                    target.room = owner->getRoomIndex();
                    target.pos.x = p.x;
                    target.pos.z = p.z;
                    if (smooth)
                        target.pos.y += (p.y - target.pos.y) * Core::deltaTime * 5.0f;
                    else
                        target.pos.y = p.y;

                    float offset = (mode == MODE_COMBAT ? CAM_OFFSET_COMBAT : CAM_OFFSET_FOLLOW) * cosf(targetAngle.x);

                    vec3 dir = vec3(targetAngle.x, targetAngle.y) * offset;
                    to.pos  = target.pos - dir;
                    to.room = target.room;

                    traceClip(offset, to);
                }

                move(to);

                if (timer <= 0.0f)
                    resetTarget();

                mViewInv = mat4(eye.pos, target.pos, vec3(0, -1, 0));
            } else
                updateFirstPerson();

            if (Core::settings.detail.vr) {
                mViewInv = mViewInv * Input::hmd.eye[0];
            }
        }
        updateListener();
        
        smooth = true;
    }

    virtual void setup(bool calcMatrices) {
        if (calcMatrices) {
            Core::mViewInv = mViewInv;

            if (Core::settings.detail.vr)
                Core::mViewInv = Core::mViewInv * Input::hmd.eye[Core::eye == -1.0f ? 0 : 1];

            if (reflectPlane) {
                Core::mViewInv = mat4(*reflectPlane) * Core::mViewInv;
                Core::mViewInv.scale(vec3(1.0f, -1.0f, 1.0f));
            }

            Core::mView = Core::mViewInv.inverse();
            if (shake > 0.0f)
                Core::mView.translate(vec3(0.0f, sinf(shake * PI * 7) * shake * 48.0f, 0.0f));

            if (Core::settings.detail.stereo == Core::Settings::STEREO_ON)
                Core::mView.translate(Core::mViewInv.right().xyz() * (-Core::eye * (firstPerson ? 8.0f : 32.0f) ));

            if (Core::settings.detail.vr)
                Core::mProj = Input::hmd.proj[Core::eye == -1.0f ? 0 : 1];
            else
                Core::mProj = mat4(fov, aspect, znear, zfar);
        }

        Core::setViewProj(Core::mView, Core::mProj);

        Core::viewPos = Core::mViewInv.offset().xyz();

        frustum->pos = Core::viewPos;
        frustum->calcPlanes(Core::mViewProj);
    }

    void changeView(bool firstPerson) {
        this->firstPerson = firstPerson;

        if (firstPerson)
            smooth = false;

        advAngle = vec3(0.0f);
        advTimer = 0.0f;

        fov   = firstPerson ? 90.0f : 65.0f;
        znear = firstPerson ? 8.0f  : 32.0f;
        #ifdef _PSP
            znear = 256.0f;
        #endif
        zfar  = 45.0f * 1024.0f;
    }
};

#endif