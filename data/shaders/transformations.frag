#version 330 core
out vec4 FragColor;

in vec3 color;
in vec2 TexCoord;

struct Material {
   sampler2D texture_albedo;
};

uniform Material material;

void main()
{
   FragColor = texture(material.texture_albedo, TexCoord);
}
