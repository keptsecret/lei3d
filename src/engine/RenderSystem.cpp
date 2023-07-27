#include "engine/RenderSystem.hpp"

#include "util/GLDebug.hpp"
#include <array>

namespace lei3d {

void RenderSystem::initialize(int width, int height) {
    scwidth = width;
    scheight = height;

    forwardShader = Shader("./data/shaders/forward.vert", "./data/shaders/forward.frag");
    postprocessShader = Shader("./data/shaders/screenspace_quad.vert", "./data/shaders/postprocess.frag");

    glGenVertexArrays(1, &dummyVAO);

    glGenFramebuffers(1, &FBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, FBO);

    glGenTextures(1, &rawTexture);
    glGenTextures(1, &saturationMask);
    glGenTextures(1, &depthStencilTexture);
    glGenTextures(1, &finalTexture);

    // lighting pass
    glBindTexture(GL_TEXTURE_2D, rawTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rawTexture, 0);

    // saturation buffer
    glBindTexture(GL_TEXTURE_2D, saturationMask);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, nullptr);   // single channel for one float values
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, saturationMask, 0);

    // depth map
    glBindTexture(GL_TEXTURE_2D, depthStencilTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);   // probably won't need stencil
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthStencilTexture, 0);

    // post process pass target
    glBindTexture(GL_TEXTURE_2D, finalTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, finalTexture, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    ///< error checks
}

void RenderSystem::draw(const Entity& object, const SkyBox& skyBox, FlyCamera* camera) {
    // clear the blit image
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, FBO);
    glDrawBuffer(GL_COLOR_ATTACHMENT2);

    glClearColor(0.2f, 0.8f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    lightingPass(object, camera);
    environmentPass(skyBox, camera);
    postprocessPass();
}

void RenderSystem::lightingPass(const Entity& object, FlyCamera* camera) {
    forwardShader.use();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, FBO);
    std::array<GLenum, 2> drawBuffers{ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(drawBuffers.size(), drawBuffers.data());  // set attachment targets as 0 and 1

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);    // enable drawing to depth mask and depth testing
    glClearColor(0.f, 0.f, 0.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1200.0f / 1000.0f, 0.1f, 400.0f);
    forwardShader.setUniformMat4(projection, "projection");
    glm::mat4 view = camera->getCameraView();
    forwardShader.setUniformMat4(view, "view");

    glm::mat4 model = glm::mat4(1.0f);
//    model = glm::scale(model, object.transform.scale);
//    model = glm::translate(model, object.transform.position);
    forwardShader.setUniformMat4(model, "model");

    forwardShader.setVec3("camPos", camera->getPosition());

    forwardShader.setVec3("dirLight.direction", directionalLight.direction);
    forwardShader.setVec3("dirLight.color", directionalLight.color);
    forwardShader.setFloat("dirLight.intensity", directionalLight.intensity);

    object.model->Draw(forwardShader);

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
}

void RenderSystem::environmentPass(const SkyBox& skybox, FlyCamera* camera) {
    glEnable(GL_DEPTH_TEST);
    GLCall(glDepthFunc(GL_LEQUAL)); // we change the depth function here to it passes when testingdepth value is equal to what is current stored
    skybox.skyboxShader.use();
    glm::mat4 view = glm::mat4(glm::mat3(camera->getCameraView()));
    skybox.skyboxShader.setUniformMat4(view, "view");
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1200.0f / 1000.0f, 0.1f, 400.0f);
    skybox.skyboxShader.setUniformMat4(projection, "projection");
    // -- render the skybox cube
    GLCall(glBindVertexArray(skybox.skyboxVAO));
    GLCall(glActiveTexture(GL_TEXTURE0)); //! could be the problem
    GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, skybox.cubeMapTexture));
    GLCall(glDrawArrays(GL_TRIANGLES, 0, 36));
    GLCall(glBindVertexArray(0));
    GLCall(glDepthFunc(GL_LESS)); // set depth function back to normal
    glDisable(GL_DEPTH_TEST);
}

void RenderSystem::postprocessPass() {
    postprocessShader.use();

    glDrawBuffer(GL_COLOR_ATTACHMENT2);

    // draw a full screen quad, sample from rendered textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rawTexture);   // 0
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, saturationMask);   // 1
    postprocessShader.setInt("RawFinalImage", 0);
    postprocessShader.setInt("SaturationMask", 1);  // match active texture bindings

    glBindVertexArray(dummyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, FBO);
    glReadBuffer(GL_COLOR_ATTACHMENT2);

    // blit to screen
    glBlitFramebuffer(0, 0, scwidth, scheight, 0, 0, scwidth, scheight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

} // lei3d