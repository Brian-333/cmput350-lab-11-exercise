#version 120

uniform sampler1D transforms;
uniform int numTransforms;

attribute vec2 position;
attribute vec2 texCoords;
attribute float spriteNumber;

varying vec2 texCoordsOut;

// Given a world position (for this lab, bottom-left is (0, 0), top-right of window is (1280, 720)),
// convert it to be in [-1, 1]^2, where y = -1 is the bottom and x = -1 is the left side.
vec2 cameraPos(vec2 worldPos) {
    return worldPos / vec2(1280.0, 720.0) * 2.0 - 1.0;
}

void main() {
    // sample at texel centers; each transform is (scale.x, scale.y, translation.x, translation.y)
    vec4 transform = texture1D(transforms, (spriteNumber + 0.5) / float(numTransforms));
    vec2 worldPos = position * transform.xy + transform.zw;
    gl_Position = vec4(cameraPos(worldPos), 0.0, 1.0);
    // getBoundsUV flips v to a bottom-left origin, but sf::Texture stores image row 0 at v = 0
    texCoordsOut = vec2(texCoords.x, 1.0 - texCoords.y);
}
