#ifndef SPRITE_MANAGER_H
#define SPRITE_MANAGER_H

#include <cstddef>

#include "basic_transform.h"
#include "texture_atlas.h"
#include "vector_util.h"
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>

#define GL_SILENCE_DEPRECATION  // silences macOS OpenGL deprecation warnings

#define glCheck(expr)                                                                              \
    expr;                                                                                          \
    if (int e = glGetError()) {                                                                    \
        std::cerr << "OpenGL error: " << e << " in file " __FILE__ << ", line number " << __LINE__ \
                  << '\n';                                                                         \
        abort();                                                                                   \
    }

using SpriteID = size_t;

class SpriteManager {
public:
    SpriteManager(const sf::Vector2u& atlasSize, uint padding)
        : mVertices(sf::PrimitiveType::Triangles),
          mNumSprites{0},
          mAtlas(atlasSize, padding),
          mShader(std::filesystem::path{"main.vert"}, std::filesystem::path{"main.frag"}) {
        GLuint program = mShader.getNativeHandle();

        mPositionAttribLocation = glGetAttribLocation(program, "position");
        mTexCoordsAttribLocation = glGetAttribLocation(program, "texCoords");
        mSpriteNumberAttribLocation = glGetAttribLocation(program, "spriteNumber");

        mTextureUniformLocation = glGetUniformLocation(program, "texture");
        mTransformsUniformLocation = glGetUniformLocation(program, "transforms");
        mNumTransformsUniformLocation = glGetUniformLocation(program, "numTransforms");

        glCheck(glGenBuffers(1, &mVerticesVBO));
        glCheck(glGenBuffers(1, &mSpriteNumberVBO));
        glCheck(glGenTextures(1, &mTransformsTexture));
    }

    ~SpriteManager() {
        glDeleteBuffers(1, &mVerticesVBO);
        glDeleteBuffers(1, &mSpriteNumberVBO);
        glDeleteTextures(1, &mTransformsTexture);
    }

    // Owns GL objects, so copying would cause double deletion.
    SpriteManager(const SpriteManager&) = delete;
    SpriteManager& operator=(const SpriteManager&) = delete;

    TextureID registerTexture(const std::string& filepath) {
        return mAtlas.registerTexture(filepath);
    }

    SpriteID createSprite(TextureID textureID) {
        SpriteID spriteID = mNumSprites++;
        mVertices.resize(mVertices.getVertexCount() + VERTICES_PER_SPRITE);
        writeQuad(spriteID, textureID);
        mTransforms.emplace_back();
        mSpriteNumbers.insert(mSpriteNumbers.end(), VERTICES_PER_SPRITE,
                              static_cast<float>(spriteID));
        return spriteID;
    }

    void setTexture(SpriteID sprite, TextureID newTexture) { writeQuad(sprite, newTexture); }

    void draw(sf::RenderTarget& target, sf::RenderStates states) const {
        sf::VertexArray transformed = mVertices;
        for (size_t i = 0; i < transformed.getVertexCount(); ++i) {
            transformed[i].position =
                mTransforms[i / VERTICES_PER_SPRITE].applyPoint(transformed[i].position);
        }
        mShader.setUniform("texture", sf::Shader::CurrentTexture);
        states.texture = &mAtlas.getTexture();
        states.shader = &mShader;
        target.draw(transformed, states);
    }

    void drawGL() {
        if (mNumSprites == 0) {
            return;
        }
        glCheck(glUseProgram(mShader.getNativeHandle()));

        // Vertex positions + UVs, interleaved inside sf::Vertex
        glCheck(glBindBuffer(GL_ARRAY_BUFFER, mVerticesVBO));
        glCheck(glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(mVertices.getVertexCount() * sizeof(sf::Vertex)),
                             &mVertices[0], GL_DYNAMIC_DRAW));
        glCheck(glVertexAttribPointer(mPositionAttribLocation, 2, GL_FLOAT, GL_FALSE,
                                      sizeof(sf::Vertex),
                                      reinterpret_cast<void*>(offsetof(sf::Vertex, position))));
        glCheck(glEnableVertexAttribArray(mPositionAttribLocation));
        glCheck(glVertexAttribPointer(mTexCoordsAttribLocation, 2, GL_FLOAT, GL_FALSE,
                                      sizeof(sf::Vertex),
                                      reinterpret_cast<void*>(offsetof(sf::Vertex, texCoords))));
        glCheck(glEnableVertexAttribArray(mTexCoordsAttribLocation));

        // Sprite numbers
        glCheck(glBindBuffer(GL_ARRAY_BUFFER, mSpriteNumberVBO));
        glCheck(glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(mSpriteNumbers.size() * sizeof(float)),
                             mSpriteNumbers.data(), GL_DYNAMIC_DRAW));
        glCheck(glVertexAttribPointer(mSpriteNumberAttribLocation, 1, GL_FLOAT, GL_FALSE, 0,
                                      nullptr));
        glCheck(glEnableVertexAttribArray(mSpriteNumberAttribLocation));

        // Transforms on texture unit 1, one RGBA32F texel per BasicTransform
        glCheck(glActiveTexture(GL_TEXTURE1));
        glCheck(glBindTexture(GL_TEXTURE_1D, mTransformsTexture));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glCheck(glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F_ARB,
                             static_cast<GLsizei>(mTransforms.size()), 0, GL_RGBA, GL_FLOAT,
                             mTransforms.data()));

        // Atlas on texture unit 0
        glCheck(glActiveTexture(GL_TEXTURE0));
        glCheck(glBindTexture(GL_TEXTURE_2D, mAtlas.getTexture().getNativeHandle()));

        glCheck(glUniform1i(mTextureUniformLocation, 0));
        glCheck(glUniform1i(mTransformsUniformLocation, 1));
        glCheck(glUniform1i(mNumTransformsUniformLocation, static_cast<GLint>(mTransforms.size())));

        glCheck(glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.getVertexCount())));

        glCheck(glBindBuffer(GL_ARRAY_BUFFER, 0));
    }

    BasicTransform& getTransform(SpriteID id) {
        assert(id < mTransforms.size());
        return mTransforms[id];
    }

    const TextureAtlas& getAtlas() const { return mAtlas; }

private:
    static constexpr size_t VERTICES_PER_SPRITE = 6;

    // Writes the two triangles of the given sprite: a quad from (0, 0) to the texture's size in
    // world units, with UVs covering the texture's unpadded region of the atlas.
    void writeQuad(SpriteID id, TextureID textureID) {
        assert(id < mNumSprites);
        sf::Vector2f size(mAtlas.getSizePixels(textureID));
        sf::FloatRect uv = mAtlas.getBoundsUV(textureID);

        sf::Vector2f posBL{0.f, 0.f};
        sf::Vector2f posBR{size.x, 0.f};
        sf::Vector2f posTR{size.x, size.y};
        sf::Vector2f posTL{0.f, size.y};

        sf::Vector2f uvBL = uv.position;
        sf::Vector2f uvBR{uv.position.x + uv.size.x, uv.position.y};
        sf::Vector2f uvTR = uv.position + uv.size;
        sf::Vector2f uvTL{uv.position.x, uv.position.y + uv.size.y};

        size_t base = id * VERTICES_PER_SPRITE;
        mVertices[base + 0] = sf::Vertex{posBL, sf::Color::White, uvBL};
        mVertices[base + 1] = sf::Vertex{posBR, sf::Color::White, uvBR};
        mVertices[base + 2] = sf::Vertex{posTR, sf::Color::White, uvTR};
        mVertices[base + 3] = sf::Vertex{posBL, sf::Color::White, uvBL};
        mVertices[base + 4] = sf::Vertex{posTR, sf::Color::White, uvTR};
        mVertices[base + 5] = sf::Vertex{posTL, sf::Color::White, uvTL};
    }

    sf::VertexArray mVertices;
    std::vector<BasicTransform> mTransforms;
    size_t mNumSprites;
    TextureAtlas mAtlas;
    mutable sf::Shader mShader;

    // Use the below only in Batched rendering (part 2):
    std::vector<float> mSpriteNumbers;

    GLint mTextureUniformLocation;
    GLint mTransformsUniformLocation;
    GLint mNumTransformsUniformLocation;

    GLint mPositionAttribLocation;
    GLint mTexCoordsAttribLocation;
    GLint mSpriteNumberAttribLocation;

    GLuint mVerticesVBO;
    GLuint mSpriteNumberVBO;
    GLuint mTransformsTexture;
};

#endif  // SPRITE_MANAGER_H
