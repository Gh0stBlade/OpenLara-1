#ifndef H_MESH
#define H_MESH

#include "core.h"
#include "format.h"

TR::ObjectTexture barTile[5 /* UI::BAR_MAX */];
TR::ObjectTexture &whiteTile = barTile[4]; // BAR_WHITE

struct MeshRange {
    int iStart;
    int iCount;
    int vStart;
    int aIndex;

    uint16 tile;
    uint16 clut;

    MeshRange() : iStart(0), iCount(0), vStart(0), aIndex(-1), tile(0), clut(0) {}

#ifdef FFP
    #ifdef _PSP
        void setup() const {
            Core::active.vBuffer += vStart;
        }
    #else
        void setup() const {
            VertexGPU *v = (VertexGPU*)NULL + vStart;
            glTexCoordPointer (2, GL_SHORT,         sizeof(*v), &v->texCoord);
            glColorPointer    (4, GL_UNSIGNED_BYTE, sizeof(*v), &v->light);
            glNormalPointer   (   GL_SHORT,         sizeof(*v), &v->normal);
            glVertexPointer   (3, GL_SHORT,         sizeof(*v), &v->coord);
        }
    #endif

    void bind(uint32 *VAO) const {}
#else
    void setup() const {
        glEnableVertexAttribArray(aCoord);
        glEnableVertexAttribArray(aNormal);
        glEnableVertexAttribArray(aTexCoord);
        glEnableVertexAttribArray(aParam);
        glEnableVertexAttribArray(aColor);
        glEnableVertexAttribArray(aLight);

        VertexGPU *v = (VertexGPU*)NULL + vStart;
        glVertexAttribPointer(aCoord,    4, GL_SHORT,         false, sizeof(*v), &v->coord);
        glVertexAttribPointer(aNormal,   4, GL_SHORT,         true,  sizeof(*v), &v->normal);
        glVertexAttribPointer(aTexCoord, 4, GL_SHORT,         true,  sizeof(*v), &v->texCoord);
        glVertexAttribPointer(aParam,    4, GL_UNSIGNED_BYTE, false, sizeof(*v), &v->param);
        glVertexAttribPointer(aColor,    4, GL_UNSIGNED_BYTE, true,  sizeof(*v), &v->color);
        glVertexAttribPointer(aLight,    4, GL_UNSIGNED_BYTE, true,  sizeof(*v), &v->light);
    }

    void bind(GLuint *VAO) const {
        GLuint vao = aIndex == -1 ? 0 : VAO[aIndex];
        if (Core::support.VAO && Core::active.VAO != vao)
            glBindVertexArray(Core::active.VAO = vao);
    }
#endif
};

#define PLANE_DETAIL 48
#define CIRCLE_SEGS  16

#define DYN_MESH_QUADS 1024

struct Mesh {
    #ifdef _PSP
        Index       *iBuffer;
        VertexGPU   *vBuffer;
    #else
        GLuint      ID[2];
        GLuint      *VAO;
    #endif

    int     iCount;
    int     vCount;
    int     aCount;
    int     aIndex;
    bool    cmdBufAlloc;

    Mesh(int iCount, int vCount) :  iCount(iCount), vCount(vCount), aCount(0), aIndex(-1), cmdBufAlloc(true) {
    #ifdef _PSP
        iBuffer =     (Index*)sceGuGetMemory(iCount * sizeof(Index)); 
        vBuffer = (VertexGPU*)sceGuGetMemory(vCount * sizeof(VertexGPU)); 
    #endif
    }

    Mesh(Index *indices, int iCount, Vertex *vertices, int vCount, int aCount) : iCount(iCount), vCount(vCount), aCount(aCount), aIndex(0), cmdBufAlloc(false) {
    #ifdef _PSP
        #ifdef EDRAM_MESH
            iBuffer =     (Index*)Core::allocEDRAM(iCount * sizeof(Index)); 
            vBuffer = (VertexGPU*)Core::allocEDRAM(vCount * sizeof(VertexGPU)); 
        #else
            iBuffer = new Index[iCount];
            vBuffer = new VertexGPU[vCount];
        #endif

        update(indices, iCount, vertices, vCount);
    #else
        VAO = NULL;
        if (Core::support.VAO)
            glBindVertexArray(Core::active.VAO = 0);

        glGenBuffers(2, ID);
        bind(true);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, iCount * sizeof(Index), indices, GL_STATIC_DRAW);
        glBufferData(GL_ARRAY_BUFFER, vCount * sizeof(Vertex), vertices, GL_STATIC_DRAW);

        if (Core::support.VAO && aCount) {
            VAO = new GLuint[aCount];
            glGenVertexArrays(aCount, VAO);
        }
    #endif
    }

    void update(Index *indices, int iCount, Vertex *vertices, int vCount) {
    #ifdef _PSP
        if (indices)
            memcpy(iBuffer, indices, iCount * sizeof(indices[0]));
        
        if (vertices) {
            Vertex    *src = vertices;
            VertexGPU *dst = vBuffer;

            for (int i = 0; i < vCount; i++) {
                dst->texCoord = short2(src->texCoord.x, src->texCoord.y);
                dst->color    = ubyte4(src->light.x, src->light.y, src->light.z, 255); //color;
                dst->normal   = src->normal;
                dst->coord    = src->coord;

                dst++;
                src++;
            }
        }
    #else
        if (Core::support.VAO)
            glBindVertexArray(Core::active.VAO = 0);

        if (indices && iCount) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Core::active.iBuffer = ID[0]);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, iCount * sizeof(Index), indices);
        }
        if (vertices && vCount) {
            glBindBuffer(GL_ARRAY_BUFFER, Core::active.vBuffer = ID[1]);
            glBufferSubData(GL_ARRAY_BUFFER, 0, vCount * sizeof(Vertex), vertices);
        }
    #endif
    }

    virtual ~Mesh() {
    #ifdef _PSP
        #ifndef EDRAM_MESH
            if (!cmdBufAlloc) {
                delete[] iBuffer;
                delete[] vBuffer;
            }
        #endif
    #else
        if (VAO) {
            glDeleteVertexArrays(aCount, VAO);
            delete[] VAO;
        }
        glDeleteBuffers(2, ID);
    #endif
    }

    void initRange(MeshRange &range) {
    #ifndef _PSP
        if (Core::support.VAO) {
            ASSERT(aIndex < aCount);
            range.aIndex = aIndex++;
            range.bind(VAO);
            bind(true);
            range.setup();
        } else
    #endif
            range.aIndex = -1;
    }

    void bind(bool force = false) {
    #ifdef _PSP
        Core::active.iBuffer = iBuffer;
        Core::active.vBuffer = vBuffer;
    #else
        if (force || Core::active.iBuffer != ID[0])
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Core::active.iBuffer = ID[0]);
        if (force || Core::active.vBuffer != ID[1])
            glBindBuffer(GL_ARRAY_BUFFER, Core::active.vBuffer = ID[1]);
    #endif
    }

    void render(const MeshRange &range) {
    #ifndef _PSP
        range.bind(VAO);
    #endif

        if (range.aIndex == -1) {
            bind();
            range.setup();
        };

        Core::DIP(range.iStart, range.iCount);
    }
};


#define CHECK_NORMAL(n) \
        if (!(n.x | n.y | n.z)) {\
            vec3 o(mVertices[f.vertices[0]]);\
            vec3 a = o - mVertices[f.vertices[1]];\
            vec3 b = o - mVertices[f.vertices[2]];\
            o = b.cross(a).normal() * 16300.0f;\
            n.x = (int)o.x;\
            n.y = (int)o.y;\
            n.z = (int)o.z;\
        }\

#define CHECK_ROOM_NORMAL(n) \
            vec3 o(d.vertices[f.vertices[0]].vertex);\
            vec3 a = o - d.vertices[f.vertices[1]].vertex;\
            vec3 b = o - d.vertices[f.vertices[2]].vertex;\
            o = b.cross(a).normal() * 16300.0f;\
            n.x = (int)o.x;\
            n.y = (int)o.y;\
            n.z = (int)o.z;

float intensityf(uint16 lighting) {
    if (lighting > 0x1FFF) return 1.0f;
    float lum = 1.0f - (lighting >> 5) / 255.0f;
    //return powf(lum, 2.2f); // gamma to linear space
    return lum;// * lum; // gamma to "linear" space
}

uint8 intensity(int lighting) {
    return uint8(intensityf(lighting) * 255);
}

struct MeshBuilder {
#ifndef _PSP
    MeshRange dynRange;
    Mesh      *dynMesh;
#endif

    Mesh      *mesh;
    Texture   *atlas;
    TR::Level *level;

// level
    struct Geometry {
        int       count;
        MeshRange ranges[100];

        Geometry() : count(0) {}

        void finish(int iCount) {
            MeshRange *range = count ? &ranges[count - 1] : NULL;

            if (range) {
                range->iCount = iCount - range->iStart;
                if (!range->iCount)
                    count--;
            }
        }

        bool validForTile(uint16 tile, uint16 clut) {
        #ifdef SPLIT_BY_TILE
            if (!count) return false;
            MeshRange &range = ranges[count - 1];

            return (tile == range.tile
                #ifdef SPLIT_BY_CLUT
                    && clut == range.clut
                #endif
                    );
        #else
            return count != 0;
        #endif
        }

        MeshRange* getNextRange(int vStart, int iCount, uint16 tile, uint16 clut) {
            MeshRange *range = count ? &ranges[count - 1] : NULL;

            if (range)
                range->iCount = iCount - range->iStart;
    
            if (!range || range->iCount) {
                ASSERT(count < COUNT(ranges));
                range = &ranges[count++];
            }

            range->vStart = vStart;
            range->iStart = iCount;
            range->tile   = tile;
            range->clut   = clut;

            return range;
        }
    };

    struct RoomRange {
        Geometry  geometry[3]; // opaque, double-side alpha, additive
        MeshRange sprites;
        int       split;
    } *rooms;

    struct ModelRange {
        int      parts[3][32];
        Geometry geometry[3];
    } *models;

    struct SpriteRange {
        MeshRange sprites;
        int       transp;
    } *sequences;

// procedured
    MeshRange shadowBlob;
    MeshRange quad, circle;
    MeshRange plane;

    vec2 *animTexRanges;
    vec2 *animTexOffsets;

    int animTexRangesCount;
    int animTexOffsetsCount;

    int transparent;

    enum {
        BLEND_NONE  = 1,
        BLEND_ALPHA = 2,
        BLEND_ADD   = 4,
    };

    MeshBuilder(TR::Level &level, Texture *atlas) : atlas(atlas), level(&level) {
    #ifndef _PSP
        dynMesh = new Mesh(NULL, DYN_MESH_QUADS * 6, NULL, DYN_MESH_QUADS * 4, 1);
        dynRange.vStart = 0;
        dynRange.iStart = 0;
        dynMesh->initRange(dynRange);
    #endif

        initAnimTextures(level);

    // allocate room geometry ranges
        rooms = new RoomRange[level.roomsCount];

        int iCount = 0, vCount = 0;

    // sort room faces by material
        for (int i = 0; i < level.roomsCount; i++) {
            TR::Room::Data &data = level.rooms[i].data;
            sort(data.faces, data.fCount);
        // sort room sprites by material
            sort(data.sprites, data.sCount);
        }

    // sort mesh faces by material
        for (int i = 0; i < level.meshesCount; i++) {
            TR::Mesh &mesh = level.meshes[i];
            sort(mesh.faces, mesh.fCount);
        }

    // get size of mesh for rooms (geometry & sprites)
        int vStartRoom = vCount;

        for (int i = 0; i < level.roomsCount; i++) {
            TR::Room       &r = level.rooms[i];
            TR::Room::Data &d = r.data;

            int vStartCount = vCount;

            iCount += 2 * (d.rCount * 6 + d.tCount * 3);
            vCount += d.rCount * 4 + d.tCount * 3;

            if (Core::settings.detail.water > Core::Settings::LOW)
                roomRemoveWaterSurfaces(r, iCount, vCount);

            for (int j = 0; j < r.meshesCount; j++) {
                TR::Room::Mesh &m = r.meshes[j];
                TR::StaticMesh *s = &level.staticMeshes[m.meshIndex];
                if (!level.meshOffsets[s->mesh]) continue;
                TR::Mesh &mesh = level.meshes[level.meshOffsets[s->mesh]];

                iCount += 2 * (mesh.rCount * 6 + mesh.tCount * 3);
                vCount += mesh.rCount * 4 + mesh.tCount * 3;
            }

        #ifdef MERGE_SPRITES
            iCount += d.sCount * 6;
            vCount += d.sCount * 4;
        #endif

            if (vCount - vStartRoom > 0xFFFF) {
                vStartRoom = vStartCount;
                rooms[i].split = true;
            } else
                rooms[i].split = false;
        }

    // get models info
        models = new ModelRange[level.modelsCount];
        for (int i = 0; i < level.modelsCount; i++) {
            TR::Model &model = level.models[i];
            for (int j = 0; j < model.mCount; j++) {
                int index = level.meshOffsets[model.mStart + j];
                if (!index && model.mStart + j > 0) 
                    continue;
                TR::Mesh &mesh = level.meshes[index];
                iCount += 2 * (mesh.rCount * 6 + mesh.tCount * 3);
                vCount += mesh.rCount * 4 + mesh.tCount * 3;
            }
        }

    // get size of mesh for sprite sequences
        sequences = new SpriteRange[level.spriteSequencesCount];
        for (int i = 0; i < level.spriteSequencesCount; i++) {
            sequences[i].transp = 1; // alpha blending by default
        #ifdef MERGE_SPRITES
            iCount += level.spriteSequences[i].sCount * 6;
            vCount += level.spriteSequences[i].sCount * 4;
        #endif
        }

    // shadow blob mesh (8 triangles, 8 vertices)
        iCount += 8 * 3 * 3;
        vCount += 8 * 2 + 1;

    // quad (post effect filter)
        iCount += 2 * 3;
        vCount += 4;

    // circle
        iCount += CIRCLE_SEGS * 3;
        vCount += CIRCLE_SEGS + 1;

    // detailed plane
    #ifdef GENERATE_WATER_PLANE
        iCount += PLANE_DETAIL * 2 * PLANE_DETAIL * 2 * (2 * 3);
        vCount += (PLANE_DETAIL * 2 + 1) * (PLANE_DETAIL * 2 + 1);
    #endif

    // make meshes buffer (single vertex buffer object for all geometry & sprites on level)
        Index  *indices  = new Index[iCount];
        Vertex *vertices = new Vertex[vCount];
        iCount = vCount = 0;
        int aCount = 0;

    // build rooms
        vStartRoom = vCount;
        aCount++;

        for (int i = 0; i < level.roomsCount; i++) {
            TR::Room &room = level.rooms[i];
            TR::Room::Data &d = room.data;
            RoomRange &range = rooms[i];

            if (range.split) {
                vStartRoom = vCount;
                aCount++;
            }

            for (int transp = 0; transp < 3; transp++) { // opaque, opacity
                int blendMask = getBlendMask(transp);

                Geometry &geom = range.geometry[transp];

            // rooms geometry
                buildRoom(geom, blendMask, room, level, indices, vertices, iCount, vCount, vStartRoom);

            // static meshes
                for (int j = 0; j < room.meshesCount; j++) {
                    TR::Room::Mesh &m = room.meshes[j];
                    TR::StaticMesh *s = &level.staticMeshes[m.meshIndex];
                    if (!level.meshOffsets[s->mesh]) continue;
                    TR::Mesh &mesh = level.meshes[level.meshOffsets[s->mesh]];

                    int x = m.x - room.info.x;
                    int y = m.y;
                    int z = m.z - room.info.z;
                    int d = m.rotation.value / 0x4000;
                    buildMesh(geom, blendMask, mesh, level, indices, vertices, iCount, vCount, vStartRoom, 0, x, y, z, d, m.color);
                }

                geom.finish(iCount);
            }

        // rooms sprites
        #ifdef MERGE_SPRITES
            range.sprites.vStart = vStartRoom;
            range.sprites.iStart = iCount;
            for (int j = 0; j < d.sCount; j++) {
                TR::Room::Data::Sprite &f = d.sprites[j];
                TR::Room::Data::Vertex &v = d.vertices[f.vertex];
                TR::SpriteTexture &sprite = level.spriteTextures[f.texture];

                addSprite(indices, vertices, iCount, vCount, vStartRoom, v.vertex.x, v.vertex.y, v.vertex.z, sprite, v.color, v.color);
            }
            range.sprites.iCount = iCount - range.sprites.iStart;
        #else
            range.sprites.iCount = d.sCount * 6;
        #endif
        }
        ASSERT(vCount - vStartRoom <= 0xFFFF);

    // build models geometry
        int vStartModel = vCount;
        aCount++;

        TR::Color32 COLOR_WHITE(255, 255, 255, 255);

        for (int i = 0; i < level.modelsCount; i++) {
            TR::Model &model = level.models[i];

            for (int transp = 0; transp < 3; transp++) {
                Geometry &geom = models[i].geometry[transp];

                int blendMask = getBlendMask(transp);

                for (int j = 0; j < model.mCount; j++) {
                    #ifndef MERGE_MODELS
                        models[i].parts[transp][j] = geom.count;
                    #endif

                    int index = level.meshOffsets[model.mStart + j];
                    if (index || model.mStart + j <= 0) {
                        TR::Mesh &mesh = level.meshes[index];
                        #ifndef MERGE_MODELS
                            geom.getNextRange(vStartModel, iCount, 0xFFFF, 0xFFFF);
                        #endif
                        buildMesh(geom, blendMask, mesh, level, indices, vertices, iCount, vCount, vStartModel, j, 0, 0, 0, 0, COLOR_WHITE);
                    }

                    #ifndef MERGE_MODELS
                        geom.finish(iCount);
                        models[i].parts[transp][j] = geom.count - models[i].parts[transp][j];
                    #endif
                }

                #ifdef MERGE_MODELS
                    geom.finish(iCount);
                    models[i].parts[transp][0] = geom.count;
                #endif
            }

            //int transp = TR::Entity::fixTransp(model.type);

            if (model.type == TR::Entity::SKY) {
                ModelRange &m = models[i];
                m.geometry[0].ranges[0].iCount = iCount - models[i].geometry[0].ranges[0].iStart;
                m.geometry[1].ranges[0].iCount = 0;
                m.geometry[2].ranges[0].iCount = 0;
            // remove bottom triangles from skybox
                if (m.geometry[0].ranges[0].iCount && ((level.version & TR::VER_TR3)))
                    m.geometry[0].ranges[0].iCount -= 16 * 3;
            }
        }
        ASSERT(vCount - vStartModel <= 0xFFFF);

    // build sprite sequences
    #ifdef MERGE_SPRITES
        int vStartSprite = vCount;
        aCount++;

        for (int i = 0; i < level.spriteSequencesCount; i++) {
            MeshRange &range = sequences[i].sprites;
            range.vStart = vStartSprite;
            range.iStart = iCount;
            for (int j = 0; j < level.spriteSequences[i].sCount; j++) {
                TR::SpriteTexture &sprite = level.spriteTextures[level.spriteSequences[i].sStart + j];
                addSprite(indices, vertices, iCount, vCount, vStartSprite, 0, 0, 0, sprite, TR::Color32(255, 255, 255, 255), TR::Color32(255, 255, 255, 255));
            }
            range.iCount = iCount - range.iStart;
        }
        ASSERT(vCount - vStartSprite <= 0xFFFF);
    #endif

    // build common primitives
        int vStartCommon = vCount;
        aCount++;

        shadowBlob.vStart = vStartCommon;
        shadowBlob.iStart = iCount;
        shadowBlob.iCount = 8 * 3 * 3;
        for (int i = 0; i < 9; i++) {
            Vertex &v0 = vertices[vCount + i * 2 + 0];
            v0.normal    = short4( 0, -1, 0, 32767 );
            v0.texCoord  = short4( whiteTile.texCoord[0].x, whiteTile.texCoord[0].y, 32767, 32767 );
            v0.param     = v0.color = v0.light = ubyte4( 0, 0, 0, 0 );

            if (i == 8) {
                v0.coord = short4( 0, 0, 0, 0 );
                break;
            }

            float a = i * (PI / 4.0f) + (PI / 8.0f);
            float c = cosf(a);
            float s = sinf(a);
            short c0 = short(c * 256.0f);
            short s0 = short(s * 256.0f);
            short c1 = short(c * 512.0f);
            short s1 = short(s * 512.0f);
            v0.coord = short4( c0, 0, s0, 0 );

            Vertex &v1 = vertices[vCount + i * 2 + 1];
            v1 = v0;
            v1.coord = short4( c1, 0, s1, 0 );
            v0.param = ubyte4( 0, 0, 0, 0 );
            v1.color = v1.light = ubyte4( 255, 255, 255, 0 );

            int idx = iCount + i * 3 * 3;
            int j = ((i + 1) % 8) * 2;
            indices[idx++] = i * 2;
            indices[idx++] = 8 * 2;
            indices[idx++] = j;

            indices[idx++] = i * 2 + 1;
            indices[idx++] = i * 2;
            indices[idx++] = j;

            indices[idx++] = i * 2 + 1;
            indices[idx++] = j;
            indices[idx++] = j + 1;
        }
        vCount += 8 * 2 + 1;
        iCount += shadowBlob.iCount;

    // quad
        quad.vStart = vStartCommon;
        quad.iStart = iCount;
        quad.iCount = 2 * 3;

        addQuad(indices, iCount, vCount, vStartCommon, vertices, &whiteTile, false);
        vertices[vCount + 3].coord = short4( -1, -1, 0, 0 );
        vertices[vCount + 2].coord = short4(  1, -1, 1, 0 );
        vertices[vCount + 1].coord = short4(  1,  1, 1, 1 );
        vertices[vCount + 0].coord = short4( -1,  1, 0, 1 );

        vertices[vCount + 3].texCoord = short4(     0,      0, 0, 0 );
        vertices[vCount + 2].texCoord = short4( 32767,      0, 0, 0 );
        vertices[vCount + 1].texCoord = short4( 32767,  32767, 0, 0 );
        vertices[vCount + 0].texCoord = short4(     0,      0, 0, 0 );
        vertices[vCount + 3].light    =
        vertices[vCount + 2].light    =
        vertices[vCount + 1].light    =
        vertices[vCount + 0].light    = vertices[vCount + 3].color = ubyte4(255, 255, 255, 255);


        for (int i = 0; i < 4; i++) {
            Vertex &v = vertices[vCount + i];
            v.normal    = short4( 0, 0, 0, 32767 );
            v.texCoord  = short4( whiteTile.texCoord[0].x, whiteTile.texCoord[0].y, 32767, 32767 );
            v.param     = ubyte4( 0, 0, 0, 0 );
            v.color     = ubyte4( 255, 255, 255, 255 );
            v.light     = ubyte4( 255, 255, 255, 255 );
        }
        vCount += 4;

    // circle
        circle.vStart = vStartCommon;
        circle.iStart = iCount;
        circle.iCount = CIRCLE_SEGS * 3;

        vec2 pos(32767.0f, 0.0f);
        vec2 cs(cosf(PI2 / CIRCLE_SEGS), sinf(PI2 / CIRCLE_SEGS));

        int baseIdx = vCount - vStartCommon;
        for (int i = 0; i < CIRCLE_SEGS; i++) {
            Vertex &v = vertices[vCount + i];
            pos.rotate(cs);
            v.coord     = short4( short(pos.x), short(pos.y), 0, 0 );
            v.normal    = short4( 0, 0, 0, 32767 );
            v.texCoord  = short4( whiteTile.texCoord[0].x, whiteTile.texCoord[0].y, 32767, 32767 );
            v.param     = ubyte4( 0, 0, 0, 0 );
            v.color     = ubyte4( 255, 255, 255, 255 );
            v.light     = ubyte4( 255, 255, 255, 255 );

            indices[iCount++] = baseIdx + i;
            indices[iCount++] = baseIdx + (i + 1) % CIRCLE_SEGS;
            indices[iCount++] = baseIdx + CIRCLE_SEGS;
        }
        vertices[vCount + CIRCLE_SEGS] = vertices[vCount];
        vertices[vCount + CIRCLE_SEGS].coord = short4( 0, 0, 0, 0 );
        vCount += CIRCLE_SEGS + 1;

    // plane
    #ifdef GENERATE_WATER_PLANE
        plane.vStart = vStartCommon;
        plane.iStart = iCount;
        plane.iCount = PLANE_DETAIL * 2 * PLANE_DETAIL * 2 * (2 * 3);

        baseIdx = vCount - vStartCommon;
        for (int16 j = -PLANE_DETAIL; j <= PLANE_DETAIL; j++)
            for (int16 i = -PLANE_DETAIL; i <= PLANE_DETAIL; i++) {
                vertices[vCount++].coord = short4( i, j, 0, 0 );
                if (j < PLANE_DETAIL && i < PLANE_DETAIL) {
                    int idx = baseIdx + (j + PLANE_DETAIL) * (PLANE_DETAIL * 2 + 1) + i + PLANE_DETAIL;
                    indices[iCount + 0] = idx + PLANE_DETAIL * 2 + 1;
                    indices[iCount + 1] = idx + 1;
                    indices[iCount + 2] = idx;
                    indices[iCount + 3] = idx + PLANE_DETAIL * 2 + 2;
                    indices[iCount + 4] = idx + 1;
                    indices[iCount + 5] = idx + PLANE_DETAIL * 2 + 1;
                    iCount += 6;
                }
            }
        ASSERT(vCount - vStartCommon <= 0xFFFF);
    #else
        plane.iCount = 0;
    #endif

        LOG("MegaMesh (i:%d v:%d a:%d)\n", iCount, vCount, aCount);

    // compile buffer and ranges
        mesh = new Mesh(indices, iCount, vertices, vCount, aCount);
        delete[] indices;
        delete[] vertices;

        PROFILE_LABEL(BUFFER, mesh->ID[0], "Geometry indices");
        PROFILE_LABEL(BUFFER, mesh->ID[1], "Geometry vertices");

        // initialize Vertex Arrays
        MeshRange rangeRoom;
        rangeRoom.vStart = 0;
        mesh->initRange(rangeRoom);
        for (int i = 0; i < level.roomsCount; i++) {
            
            if (rooms[i].split) {
                ASSERT(rooms[i].geometry[0].count);
                rangeRoom.vStart = rooms[i].geometry[0].ranges[0].vStart;
                mesh->initRange(rangeRoom);
            }

            RoomRange &r = rooms[i];
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < r.geometry[j].count; k++)
                    r.geometry[j].ranges[k].aIndex = rangeRoom.aIndex;

            r.sprites.aIndex = rangeRoom.aIndex;
        }

        MeshRange rangeModel;
        rangeModel.vStart = vStartModel;
        mesh->initRange(rangeModel);
        for (int i = 0; i < level.modelsCount; i++)
            for (int j = 0; j < 3; j++) {
                Geometry &geom = models[i].geometry[j];
                for (int k = 0; k < geom.count; k++)
                    geom.ranges[k].aIndex = rangeModel.aIndex;
            }

    #ifdef MERGE_SPRITES
        MeshRange rangeSprite;
        rangeSprite.vStart = vStartSprite;
        mesh->initRange(rangeSprite);
        for (int i = 0; i < level.spriteSequencesCount; i++)
            sequences[i].sprites.aIndex = rangeSprite.aIndex;
    #endif

        MeshRange rangeCommon;
        rangeCommon.vStart = vStartCommon;
        mesh->initRange(rangeCommon);
        shadowBlob.aIndex = rangeCommon.aIndex;
        quad.aIndex       = rangeCommon.aIndex;
        circle.aIndex     = rangeCommon.aIndex;
        plane.aIndex      = rangeCommon.aIndex;
    }

    ~MeshBuilder() {
        delete[] animTexRanges;
        delete[] animTexOffsets;
        delete[] rooms;
        delete[] models;
        delete[] sequences;
        delete mesh;
    #ifndef _PSP
        delete dynMesh;
    #endif
    }

    inline short4 rotate(const short4 &v, int dir) {
        if (dir == 0) return v;
        short4 res = v;

        switch (dir) {
            case 1  : res.x =  v.z, res.z = -v.x; break;
            case 2  : res.x = -v.x, res.z = -v.z; break;
            case 3  : res.x = -v.z, res.z =  v.x; break;
            default : ASSERT(false);
        }
        return res;
    }

    inline short4 transform(const short4 &v, int joint, int x, int y, int z, int dir) {
        short4 res = rotate(v, dir);
        res.x += x;
        res.y += y;
        res.z += z;
        res.w = joint;
        return res;
    }

    bool isWaterSurface(int delta, int roomIndex, bool fromWater) {
        if (roomIndex != TR::NO_ROOM && delta == 0) {
            TR::Room &r = level->rooms[roomIndex];
            if (r.flags.water ^ fromWater)
                return true;
            if (r.alternateRoom > -1 && level->rooms[r.alternateRoom].flags.water ^ fromWater)
                return true;
        }
        return false;
    }

    void roomRemoveWaterSurfaces(TR::Room &room, int &iCount, int &vCount) {
        room.waterLevel = -1;

        for (int i = 0; i < room.data.fCount; i++) {
            TR::Face &f = room.data.faces[i];
            if (f.vertices[0] == 0xFFFF) continue;

            TR::Vertex &a = room.data.vertices[f.vertices[0]].vertex;
            TR::Vertex &b = room.data.vertices[f.vertices[1]].vertex;
            TR::Vertex &c = room.data.vertices[f.vertices[2]].vertex;

            if (a.y != b.y || a.y != c.y) // skip non-horizontal or non-portal plane primitive
                continue;
            
            int sx = (int(a.x) + int(b.x) + int(c.x)) / 3 / 1024;
            int sz = (int(a.z) + int(b.z) + int(c.z)) / 3 / 1024;

            TR::Room::Sector &s = room.sectors[sx * room.zSectors + sz];

            int yt = abs(a.y - s.ceiling * 256);
            int yb = abs(s.floor * 256 - a.y);

            if (yt > 0 && yb > 0) continue;

            if (isWaterSurface(yt, s.roomAbove, room.flags.water) ||
                isWaterSurface(yb, s.roomBelow, room.flags.water)) {
                f.vertices[0] = 0xFFFF; // mark as unused
                room.waterLevel = a.y;
                if (f.vCount == 4) {
                    iCount -= 6;
                    vCount -= 4;
                } else {
                    iCount -= 3;
                    vCount -= 3;
                }
            }
        }
    }

    inline int getBlendMask(int texAttribute) {
        ASSERT(texAttribute < 3);
        return 1 << texAttribute;
    }

    void buildRoom(Geometry &geom, int blendMask, const TR::Room &room, const TR::Level &level, Index *indices, Vertex *vertices, int &iCount, int &vCount, int vStart) {
        const TR::Room::Data &d = room.data;

        for (int j = 0; j < d.fCount; j++) {
            TR::Face          &f = d.faces[j];
            TR::ObjectTexture &t = level.objectTextures[f.flags.texture];

            if (f.vertices[0] == 0xFFFF) continue; // skip if marks as unused (removing water planes)

            if (!(blendMask & getBlendMask(t.attribute)))
                continue;

            if (!geom.validForTile(t.tile.index, t.clut))
                geom.getNextRange(vStart, iCount, t.tile.index, t.clut);

            addFace(indices, iCount, vCount, vStart, vertices, f, &t,
                    d.vertices[f.vertices[0]].vertex,
                    d.vertices[f.vertices[1]].vertex,
                    d.vertices[f.vertices[2]].vertex,
                    d.vertices[f.vertices[3]].vertex);

            TR::Vertex n;
            CHECK_ROOM_NORMAL(n);

            for (int k = 0; k < f.vCount; k++) {
                TR::Room::Data::Vertex &v = d.vertices[f.vertices[k]];
                vertices[vCount].coord  = short4( v.vertex.x, v.vertex.y, v.vertex.z, 0 );
                vertices[vCount].normal = short4( n.x, n.y, n.z, int16(t.attribute == 2 ? 0 : 32767) );
                vertices[vCount].color  = ubyte4( 255, 255, 255, 255 );
                TR::Color32 c = v.color;
                /*
                #ifdef FFP
                    if (room.flags.water) {
                        vertices[vCount].light  = ubyte4( int(c.r * 0.6f), int(c.g * 0.9), int(c.b * 0.9), 255 ); // apply underwater color
                    } else
                        vertices[vCount].light  = ubyte4( c.r, c.g, c.b, 255 );
                #endif
                */
                vertices[vCount].light  = ubyte4( c.r, c.g, c.b, 255 );
                vCount++;
            }
        }
    }

    bool buildMesh(Geometry &geom, int blendMask, const TR::Mesh &mesh, const TR::Level &level, Index *indices, Vertex *vertices, int &iCount, int &vCount, int vStart, int16 joint, int x, int y, int z, int dir, const TR::Color32 &light) {
        TR::Color24 COLOR_WHITE( 255, 255, 255 );
        bool isOpaque = true;

        for (int j = 0; j < mesh.fCount; j++) {
            TR::Face &f = mesh.faces[j];
            TR::ObjectTexture &t = f.colored ? whiteTile : level.objectTextures[f.flags.texture];

            if (t.attribute != 0)
                isOpaque = false;

            if (!(blendMask & getBlendMask(t.attribute)))
                continue;

            if (!geom.validForTile(t.tile.index, t.clut))
                geom.getNextRange(vStart, iCount, t.tile.index, t.clut);

            TR::Color32 c = f.colored ? level.getColor(f.flags.value) : COLOR_WHITE;

            addFace(indices, iCount, vCount, vStart, vertices, f, &t, 
                    mesh.vertices[f.vertices[0]].coord,
                    mesh.vertices[f.vertices[1]].coord,
                    mesh.vertices[f.vertices[2]].coord,
                    mesh.vertices[f.vertices[3]].coord);

            for (int k = 0; k < f.vCount; k++) {
                TR::Mesh::Vertex &v = mesh.vertices[f.vertices[k]];

                vertices[vCount].coord  = transform(v.coord, joint, x, y, z, dir);
                vertices[vCount].normal = rotate(v.normal, dir);
                vertices[vCount].normal.w = t.attribute == 2 ? 0 : 32767;
                vertices[vCount].color  = ubyte4( c.r, c.g, c.b, 255 );
                vertices[vCount].light  = ubyte4( light.r, light.g, light.b, 255 );

                vCount++;
            }
        }

        return isOpaque;
    }

    vec2 getTexCoord(const TR::ObjectTexture &tex) {
        return vec2(tex.texCoord[0].x / 32767.0f, tex.texCoord[0].y / 32767.0f);
    }

    void initAnimTextures(TR::Level &level) {
        ASSERT(level.animTexturesDataSize);

        uint16 *ptr = &level.animTexturesData[0];
        animTexRangesCount = *ptr++ + 1;
        animTexRanges = new vec2[animTexRangesCount];
        animTexRanges[0] = vec2(0.0f, 1.0f);
        animTexOffsetsCount = 1;
        for (int i = 1; i < animTexRangesCount; i++) {
            TR::AnimTexture *animTex = (TR::AnimTexture*)ptr;
            
            int start = animTexOffsetsCount;
            animTexOffsetsCount += animTex->count + 1;
            animTexRanges[i] = vec2((float)start, (float)(animTexOffsetsCount - start));

            ptr += (sizeof(animTex->count) + sizeof(animTex->textures[0]) * (animTex->count + 1)) / sizeof(uint16);
        }
        animTexOffsets = new vec2[animTexOffsetsCount];
        animTexOffsets[0] = vec2(0.0f);
        animTexOffsetsCount = 1;

        ptr = &level.animTexturesData[1];
        for (int i = 1; i < animTexRangesCount; i++) {
            TR::AnimTexture *animTex = (TR::AnimTexture*)ptr;

            vec2 first = getTexCoord(level.objectTextures[animTex->textures[0]]);
            animTexOffsets[animTexOffsetsCount++] = vec2(0.0f); // first - first for first frame %)

            for (int j = 1; j <= animTex->count; j++)
                animTexOffsets[animTexOffsetsCount++] = getTexCoord(level.objectTextures[animTex->textures[j]]) - first;

            ptr += (sizeof(animTex->count) + sizeof(animTex->textures[0]) * (animTex->count + 1)) / sizeof(uint16);
        }
    }

    TR::ObjectTexture* getAnimTexture(TR::ObjectTexture *tex, uint8 &range, uint8 &frame) {
        range = frame = 0;
        if (!level->animTexturesDataSize)
            return tex;

        uint16 *ptr = &level->animTexturesData[1];
        for (int i = 1; i < animTexRangesCount; i++) {
            TR::AnimTexture *animTex = (TR::AnimTexture*)ptr;

            for (int j = 0; j <= animTex->count; j++)
                if (tex == &level->objectTextures[animTex->textures[j]]) {
                    range = i;
                    frame = j;
                    return &level->objectTextures[animTex->textures[0]];
                }
            
            ptr += (sizeof(animTex->count) + sizeof(animTex->textures[0]) * (animTex->count + 1)) / sizeof(uint16);
        }
        
        return tex;
    }

    void addTexCoord(Vertex *vertices, int vCount, TR::ObjectTexture *tex, bool triangle) {
        uint8 range, frame;
        tex = getAnimTexture(tex, range, frame);

        int count = triangle ? 3 : 4;
        for (int i = 0; i < count; i++) {
            Vertex &v = vertices[vCount + i];
            v.texCoord = short4( tex->texCoord[i].x, tex->texCoord[i].y, 32767, 32767 );
            v.param    = ubyte4( range, frame, 0, 0 );
        }

        if (((level->version & TR::VER_PSX)) && !triangle) // TODO: swap vertices instead of rectangle indices and vertices.texCoords (WRONG lighting in TR2!)
            swap(vertices[vCount + 2].texCoord, vertices[vCount + 3].texCoord);
    }

    void addTriangle(Index *indices, int &iCount, int vCount, int vStart, Vertex *vertices, TR::ObjectTexture *tex, bool doubleSided) {
        int vIndex = vCount - vStart;

        indices[iCount + 0] = vIndex + 0;
        indices[iCount + 1] = vIndex + 1;
        indices[iCount + 2] = vIndex + 2;

        iCount += 3;

        if (doubleSided) {
            indices[iCount + 0] = vIndex + 2;
            indices[iCount + 1] = vIndex + 1;
            indices[iCount + 2] = vIndex + 0;
            iCount += 3;
        }

        if (tex) addTexCoord(vertices, vCount, tex, true);
    }

    void addQuad(Index *indices, int &iCount, int vCount, int vStart, Vertex *vertices, TR::ObjectTexture *tex, bool doubleSided) {
        int vIndex = vCount - vStart;

        indices[iCount + 0] = vIndex + 0;
        indices[iCount + 1] = vIndex + 1;
        indices[iCount + 2] = vIndex + 2;

        indices[iCount + 3] = vIndex + 0;
        indices[iCount + 4] = vIndex + 2;
        indices[iCount + 5] = vIndex + 3;

        iCount += 6;

        if (doubleSided) {
            indices[iCount + 0] = vIndex + 2;
            indices[iCount + 1] = vIndex + 1;
            indices[iCount + 2] = vIndex + 0;

            indices[iCount + 3] = vIndex + 3;
            indices[iCount + 4] = vIndex + 2;
            indices[iCount + 5] = vIndex + 0;

            iCount += 6;
        }

        if (tex) addTexCoord(vertices, vCount, tex, false);
    }

    void addQuad(Index *indices, int &iCount, int &vCount, int vStart, Vertex *vertices, TR::ObjectTexture *tex, bool doubleSided,
                 const short3 &c0, const short3 &c1, const short3 &c2, const short3 &c3) {
        addQuad(indices, iCount, vCount, vStart, vertices, tex, doubleSided);

        vec3 a = c0 - c1;
        vec3 b = c3 - c2;
        vec3 c = c0 - c3;
        vec3 d = c1 - c2;

        float aL = a.length();
        float bL = b.length();
        float cL = c.length();
        float dL = d.length();

        float ab = a.dot(b) / (aL * bL);
        float cd = c.dot(d) / (cL * dL);

        int16 tx = abs(vertices[vCount + 0].texCoord.x - vertices[vCount + 3].texCoord.x);
        int16 ty = abs(vertices[vCount + 0].texCoord.y - vertices[vCount + 3].texCoord.y);

        if (ab > cd) {
            int k = (tx > ty) ? 3 : 2;

            if (aL > bL)
                vertices[vCount + 2].texCoord[k] = vertices[vCount + 3].texCoord[k] = int16(bL / aL * 32767.0f);
            else
                vertices[vCount + 0].texCoord[k] = vertices[vCount + 1].texCoord[k] = int16(aL / bL * 32767.0f);
        } else {
            int k = (tx > ty) ? 2 : 3;

            if (cL > dL) {
                vertices[vCount + 1].texCoord[k] = vertices[vCount + 2].texCoord[k] = int16(dL / cL * 32767.0f);
            } else
                vertices[vCount + 0].texCoord[k] = vertices[vCount + 3].texCoord[k] = int16(cL / dL * 32767.0f);
        }
    }


    void addFace(Index *indices, int &iCount, int &vCount, int vStart, Vertex *vertices, const TR::Face &f, TR::ObjectTexture *tex, const short3 &a, const short3 &b, const short3 &c, const short3 &d) {
        if (f.vCount == 4)
            addQuad(indices, iCount, vCount, vStart, vertices, tex, f.flags.doubleSided, a, b, c, d);
        else
            addTriangle(indices, iCount, vCount, vStart, vertices, tex, f.flags.doubleSided);
    }


    short4 coordTransform(const vec3 &center, const vec3 &offset) {
        mat4 m = Core::mViewInv;
        m.setPos(center);

        vec3 coord = m * offset;
        return short4(int16(coord.x), int16(coord.y), int16(coord.z), 0);
    }

    void addSprite(Index *indices, Vertex *vertices, int &iCount, int &vCount, int vStart, int16 x, int16 y, int16 z, const TR::SpriteTexture &sprite, const TR::Color32 &tColor, const TR::Color32 &bColor, bool expand = false) {
        addQuad(indices, iCount, vCount, vStart, NULL, NULL, false);

        Vertex *quad = &vertices[vCount];

        int16 x0, y0, x1, y1;

        if (expand) {
            x0 = x + int16(sprite.l);
            y0 = y + int16(sprite.t);
            x1 = x + int16(sprite.r);
            y1 = y + int16(sprite.b);
        } else {
            x0 = x1 = x;
            y0 = y1 = y;
        }

        #ifndef MERGE_SPRITES
            if (!expand) {
                vec3 pos = vec3(float(x), float(y), float(z));
                quad[0].coord = coordTransform(pos, vec3( float(sprite.l), float(-sprite.t), 0 ));
                quad[1].coord = coordTransform(pos, vec3( float(sprite.r), float(-sprite.t), 0 ));
                quad[2].coord = coordTransform(pos, vec3( float(sprite.r), float(-sprite.b), 0 ));
                quad[3].coord = coordTransform(pos, vec3( float(sprite.l), float(-sprite.b), 0 ));
            } else
        #endif
        {
            quad[0].coord = short4( x0, y0, z, 0 );
            quad[1].coord = short4( x1, y0, z, 0 );
            quad[2].coord = short4( x1, y1, z, 0 );
            quad[3].coord = short4( x0, y1, z, 0 );
        }

        quad[0].normal = quad[1].normal = quad[2].normal = quad[3].normal = short4( 0, 0, 0, 0 );
        quad[0].color  = quad[1].color  = 
        quad[2].color  = quad[3].color  = ubyte4( 255, 255, 255, 255 );
        quad[0].light  = quad[1].light  = ubyte4( tColor.r, tColor.g, tColor.b, tColor.a );
        quad[2].light  = quad[3].light  = ubyte4( bColor.r, bColor.g, bColor.b, bColor.a );
        quad[0].param  = quad[1].param  = quad[2].param  = quad[3].param  = ubyte4( 0, 0, 0, 0 );

        quad[0].texCoord = short4( sprite.texCoord[0].x, sprite.texCoord[0].y, sprite.l, -sprite.t );
        quad[1].texCoord = short4( sprite.texCoord[1].x, sprite.texCoord[0].y, sprite.r, -sprite.t );
        quad[2].texCoord = short4( sprite.texCoord[1].x, sprite.texCoord[1].y, sprite.r, -sprite.b );
        quad[3].texCoord = short4( sprite.texCoord[0].x, sprite.texCoord[1].y, sprite.l, -sprite.b );

        vCount += 4;
    }

    void addBar(Index *indices, Vertex *vertices, int &iCount, int &vCount, const TR::ObjectTexture &tile, const vec2 &pos, const vec2 &size, uint32 color, uint32 color2 = 0) {
        addQuad(indices, iCount, vCount, 0, vertices, NULL, false);

        int16 minX = int16(pos.x);
        int16 minY = int16(pos.y);
        int16 maxX = int16(size.x) + minX;
        int16 maxY = int16(size.y) + minY;

        vertices[vCount + 0].coord = short4( minX, minY, 0, 0 );
        vertices[vCount + 1].coord = short4( maxX, minY, 0, 0 );
        vertices[vCount + 2].coord = short4( maxX, maxY, 0, 0 );
        vertices[vCount + 3].coord = short4( minX, maxY, 0, 0 );

        for (int i = 0; i < 4; i++) {
            Vertex &v = vertices[vCount + i];
            v.normal  = short4( 0, 0, 0, 0 );
            if (color2 != 0 && (i == 0 || i == 3))
                v.color = *((ubyte4*)&color2);
            else
                v.color = *((ubyte4*)&color);

            short2 uv = tile.texCoord[i];

            v.texCoord = short4( uv.x, uv.y, 32767, 32767 );
            v.param    = ubyte4( 0, 0, 0, 0 );
        }

        vCount += 4;
    }

    void addFrame(Index *indices, Vertex *vertices, int &iCount, int &vCount, const vec2 &pos, const vec2 &size, uint32 color1, uint32 color2) {
        short4 uv = short4( whiteTile.texCoord[0].x, whiteTile.texCoord[0].y, 32767, 32767 );

        int16 minX = int16(pos.x);
        int16 minY = int16(pos.y);
        int16 maxX = int16(size.x) + minX;
        int16 maxY = int16(size.y) + minY;

        vertices[vCount + 0].coord = short4( minX, minY, 0, 0 );
        vertices[vCount + 1].coord = short4( maxX, minY, 0, 0 );
        vertices[vCount + 2].coord = short4( maxX, int16(minY + 1), 0, 0 );
        vertices[vCount + 3].coord = short4( minX, int16(minY + 1), 0, 0 );

        vertices[vCount + 4].coord = short4( minX, minY, 0, 0 );
        vertices[vCount + 5].coord = short4( int16(minX + 1), minY, 0, 0 );
        vertices[vCount + 6].coord = short4( int16(minX + 1), maxY, 0, 0 );
        vertices[vCount + 7].coord = short4( minX, maxY, 0, 0 );

        for (int i = 0; i < 8; i++) {
            Vertex &v = vertices[vCount + i];
            v.normal   = short4( 0, 0, 0, 0 );
            v.color    = *((ubyte4*)&color1);
            v.texCoord = uv;
        }

        addQuad(indices, iCount, vCount, 0, vertices, NULL, false); vCount += 4;
        addQuad(indices, iCount, vCount, 0, vertices, NULL, false); vCount += 4;

        vertices[vCount + 0].coord = short4( minX, int16(maxY - 1), 0, 0 );
        vertices[vCount + 1].coord = short4( maxX, int16(maxY - 1), 0, 0 );
        vertices[vCount + 2].coord = short4( maxX, maxY, 0, 0 );
        vertices[vCount + 3].coord = short4( minX, maxY, 0, 0 );

        vertices[vCount + 4].coord = short4( int16(maxX - 1), minY, 0, 0 );
        vertices[vCount + 5].coord = short4( maxX, minY, 0, 0 );
        vertices[vCount + 6].coord = short4( maxX, maxY, 0, 0 );
        vertices[vCount + 7].coord = short4( int16(maxX - 1), maxY, 0, 0 );

        for (int i = 0; i < 8; i++) {
            Vertex &v = vertices[vCount + i];
            v.normal   = short4( 0, 0, 0, 0 );
            v.color    = *((ubyte4*)&color2);
            v.texCoord = uv;
        }

        addQuad(indices, iCount, vCount, 0, vertices, NULL, false); vCount += 4;
        addQuad(indices, iCount, vCount, 0, vertices, NULL, false); vCount += 4;
    }

    void bind() {
        mesh->bind();
    }
    
    void renderBuffer(Index *indices, int iCount, Vertex *vertices, int vCount) {
        #ifdef _PSP
            MeshRange dynRange;
            Mesh cmdBufMesh(iCount, vCount);
            Mesh *dynMesh = &cmdBufMesh;
        #endif
        dynRange.iStart = 0;
        dynRange.iCount = iCount;

        dynMesh->update(indices, iCount, vertices, vCount);
        dynMesh->render(dynRange);
    }

    void renderRoomGeometry(int roomIndex) {
        Geometry &geom = rooms[roomIndex].geometry[transparent];
        for (int i = 0; i < geom.count; i++) {
            MeshRange &range = geom.ranges[i];

        #ifdef SPLIT_BY_TILE
            int clutOffset = level->rooms[roomIndex].flags.water ? 512 : 0;
            atlas->bind(range.tile, range.clut + clutOffset);
        #endif

            mesh->render(range);
        }
    }

    void renderRoomSprites(int roomIndex) {
    #ifndef MERGE_SPRITES
        #ifdef SPLIT_BY_TILE
            uint16 curTile = 0xFFFF, curClut = 0xFFFF;
        #endif

        Core::mModel.identity();
        Core::mModel.setPos(Core::active.basis[0].pos);

        int vCount = 0, iCount = 0;
        Index  indices[DYN_MESH_QUADS * 6];
        Vertex vertices[DYN_MESH_QUADS * 4];

        TR::Room::Data &d = level->rooms[roomIndex].data;
        for (int j = 0; j < d.sCount; j++) {
            TR::Room::Data::Sprite &f = d.sprites[j];
            TR::Room::Data::Vertex &v = d.vertices[f.vertex];
            TR::SpriteTexture &sprite = level->spriteTextures[f.texture];

            #ifdef SPLIT_BY_TILE
                if (sprite.tile != curTile
                    #ifdef SPLIT_BY_CLUT
                        || sprite.clut != curClut
                    #endif
                ) {
                    if (iCount) {
                        renderBuffer(indices, iCount, vertices, vCount);
                        iCount = 0;
                        vCount = 0;
                    }
                    curTile = sprite.tile;
                    curClut = sprite.clut;
                    atlas->bind(curTile, curClut);
                }
            #endif

            addSprite(indices, vertices, iCount, vCount, 0, v.vertex.x, v.vertex.y, v.vertex.z, sprite, v.color, v.color);
        }

        if (iCount)
            renderBuffer(indices, iCount, vertices, vCount);
    #else
        mesh->render(rooms[roomIndex].sprites);
    #endif
    }

    void renderMesh(const MeshRange &range) {
        mesh->render(range);
    }

    void renderModel(int modelIndex, bool underwater = false) {
    #ifdef FFP
        Core::mModel.identity();

        #ifdef _PSP
            //sceGuDisable(GU_TEXTURE_2D);
            //Core::setBlending(bmNone);
            sceGuEnable(GU_LIGHTING);

            ubyte4 ambient;
            ambient.x = ambient.y = ambient.z = clamp(int(Core::active.material.y * 255), 0, 255);
            ambient.w = 255;
            sceGuAmbient(*(uint32*)&ambient);

            for (int i = 0; i < 1 /*MAX_LIGHTS*/; i++) {
                ScePspFVector3 pos;
                pos.x = Core::lightPos[i].x;
                pos.y = Core::lightPos[i].y;
                pos.z = Core::lightPos[i].z;

                sceGuLight(i, GU_POINTLIGHT, GU_DIFFUSE, &pos);

                ubyte4 color;
                color.x = clamp(int(Core::lightColor[i].x * 255), 0, 255);
                color.y = clamp(int(Core::lightColor[i].y * 255), 0, 255);
                color.z = clamp(int(Core::lightColor[i].z * 255), 0, 255);
                color.w = 255;

                sceGuLightColor(i, GU_DIFFUSE, *(uint32*)&color);
                sceGuLightAtt(i, 1.0f, 0.0f, Core::lightColor[i].w * Core::lightColor[i].w);
            }
        #else
            glEnable(GL_LIGHTING);
            glEnable(GL_COLOR_MATERIAL);

            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf((GLfloat*)&Core::mView);

            vec4 ambient(vec3(Core::active.material.y), 1.0f);
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (GLfloat*)&ambient);

            for (int i = 0; i < 1 /*MAX_LIGHTS*/; i++) {
                vec4 pos(Core::lightPos[i].xyz(), 1.0f);
                vec4 color(Core::lightColor[i].xyz(), 1.0f);
                float att = Core::lightColor[i].w;
                att *= att;

                glLightfv(GL_LIGHT0 + i, GL_POSITION, (GLfloat*)&pos);
                glLightfv(GL_LIGHT0 + i, GL_DIFFUSE,  (GLfloat*)&color);
                glLightfv(GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION, (GLfloat*)&att);
            }
        #endif
    #endif

        ASSERT(level->models[modelIndex].mCount == Core::active.basisCount);

        int part = 0;

        Geometry &geom = models[modelIndex].geometry[transparent];
    #ifdef MERGE_MODELS
        int i = 0;
    #else
        for (int i = 0; i < level->models[modelIndex].mCount; i++)
    #endif
        {
            #ifndef MERGE_MODELS
                Basis &basis = Core::active.basis[i];
                if (basis.w == -1.0f) {
                    part += models[modelIndex].parts[transparent][i];
                    continue;
                }
                #ifdef FFP
//                    Core::setMatrix(NULL, NULL, &m);
                    Core::mModel.setRot(basis.rot);
                    Core::mModel.setPos(basis.pos);
                #endif
            #endif

            for (int j = 0; j < models[modelIndex].parts[transparent][i]; j++) {
                MeshRange &range = geom.ranges[part++];

                ASSERT(range.iCount);

                #ifdef SPLIT_BY_TILE
                    int clutOffset = underwater ? 512 : 0;
                    atlas->bind(range.tile, range.clut + clutOffset);
                #endif

                mesh->render(range);
            }
        }

    #ifdef FFP
        #ifdef _PSP
            sceGuDisable(GU_LIGHTING);
        #else
            glDisable(GL_COLOR_MATERIAL);
            glDisable(GL_LIGHTING);
        #endif
    #endif
    }

    void renderSprite(int sequenceIndex, int frame) {
        #ifndef MERGE_SPRITES
            Core::mModel.identity();
            Core::mModel.setPos(Core::active.basis[0].pos);

            int vCount = 0, iCount = 0;
            Index  indices[1 * 6];
            Vertex vertices[1 * 4];

            TR::SpriteTexture &sprite = level->spriteTextures[level->spriteSequences[sequenceIndex].sStart + frame];

            uint8 ambient = clamp(int(Core::active.material.y * 255.0f), 0, 255);
            uint8 alpha   = clamp(int(Core::active.material.w * 255.0f), 0, 255);

            TR::Color32 color(ambient, ambient, ambient, alpha);
            addSprite(indices, vertices, iCount, vCount, 0, 0, 0, 0, sprite, color, color);

            #ifdef SPLIT_BY_TILE
                atlas->bind(sprite.tile, sprite.clut);
            #endif

            renderBuffer(indices, iCount, vertices, vCount);
        #else
            MeshRange range = sequences[sequenceIndex].sprites;
            range.iCount  = 6;
            range.iStart += frame * 6;
            mesh->render(range);
        #endif
    }

    void renderShadowBlob() {
        #ifdef _PSP
            sceGuDisable(GU_TEXTURE_2D);
        #endif
        
        mesh->render(shadowBlob);
        
        #ifdef _PSP
            sceGuEnable(GU_TEXTURE_2D);
        #endif
    }

    void renderQuad() {
        mesh->render(quad);
    }

    void renderCircle() {
        mesh->render(circle);
    }

    void renderPlane() {
        mesh->render(plane);
    }
};

#endif