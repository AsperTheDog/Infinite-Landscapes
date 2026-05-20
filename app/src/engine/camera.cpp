#include "camera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "camera.hpp"
#include "camera.hpp"
#include "camera.hpp"
#include "camera.hpp"

#include <algorithm>
#include <glm/gtx/transform.hpp>

#include <SDL2/SDL_keycode.h>

Camera::Camera(const glm::vec3 pos, const glm::vec3 dir, const float fov, const float near, const float far)
	: m_Position(pos), m_Front(dir), m_fov(fov), m_near(near), m_far(far)
{
	calculateRightVector();
}

void Camera::move(const glm::vec3 dir)
{
	m_Position += dir;
	calculateRightVector();
	setViewDirty();
}

void Camera::lookAt(const glm::vec3 target)
{
	m_Front = glm::normalize(target - m_Position);
	calculateRightVector();
	setViewDirty();
}

void Camera::setPosition(const glm::vec3 pos)
{
	m_Position = pos;
	calculateRightVector();
	setViewDirty();
}

void Camera::setDir(const glm::vec3 dir)
{
	m_Front = dir;
	calculateRightVector();
	setViewDirty();
}

void Camera::setScreenSize(const uint32_t width, const uint32_t height)
{
	m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
	setProjDirty();
}

void Camera::setProjectionData(const float fov, const float near, const float far)
{
	m_fov = fov;
	m_near = near;
	m_far = far;
    setProjDirty();
}

glm::vec3 Camera::getPosition() const
{
	return m_Position;
}

glm::vec4 Camera::getPositionV4() const
{
	return {m_Position, 1.0f};
}

glm::vec3 Camera::getDir() const
{
	return m_Front;
}

glm::vec3 Camera::getRight() const
{
	return m_right;
}

glm::vec3 Camera::getUp() const
{
	return glm::normalize(glm::cross(m_right, m_Front));
}

glm::vec2 Camera::getTiledPosition(const float p_TileSize) const
{
    glm::vec2 l_CameraTile = glm::vec2(m_Position.x, m_Position.z);
    l_CameraTile = glm::round(l_CameraTile / p_TileSize) * p_TileSize;

    return l_CameraTile;
}

glm::mat4& Camera::getViewMatrix()
{
	if (m_viewDirty)
	{
		m_viewMatrix = glm::lookAt(m_Position, m_Position + m_Front, glm::vec3(0, 1, 0));
		m_viewDirty = false;
	}

	return m_viewMatrix;
}

glm::mat4& Camera::getProjMatrix()
{
	if (m_projDirty)
	{
		m_projMatrix = glm::perspective(glm::radians(m_fov), 16.0f/9, m_near, m_far);
		m_projDirty = false;
	}

	return m_projMatrix;
}

glm::mat4& Camera::getVPMatrix()
{
	if (m_projDirty || m_viewDirty)
	{
		m_VPMatrix = getProjMatrix() * getViewMatrix();
	}

	return m_VPMatrix;
}

glm::mat4& Camera::getInvViewMatrix()
{
    if (m_InvViewDirty)
    {
        m_invViewMatrix = glm::inverse(getViewMatrix());
        m_InvViewDirty = false;
    }
    return m_invViewMatrix;
}

glm::mat4& Camera::getInvProjMatrix()
{
    if (m_InvProjDirty)
    {
        m_invProjMatrix = glm::inverse(getProjMatrix());
        m_InvProjDirty = false;
    }
    return m_invProjMatrix;
}

glm::mat4& Camera::getInvVPMatrix()
{
    if (m_InvVPMatrixDirty)
    {
        m_InvVPMatrix = glm::inverse(getVPMatrix());
        m_InvVPMatrixDirty = false;
    }

    return m_InvVPMatrix;
}

void Camera::mouseMoved(const float relX, const float relY)
{
    if (!m_isMouseCaptured) return;
	m_yaw += relX * m_mouseSensitivity;
    m_pitch += relY * m_mouseSensitivity;

    m_pitch = std::min(m_pitch, 89.0f);
    m_pitch = std::max(m_pitch, -89.0f);
    if (m_yaw > 360.0f)
		m_yaw -= 360.0f;
	if (m_yaw < -360.0f)
		m_yaw += 360.0f;

    glm::vec3 newFront;
    newFront.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    newFront.y = sin(glm::radians(m_pitch));
    newFront.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    setDir(newFront);
}

void Camera::keyPressed(const uint32_t key)
{
    if (!m_isMouseCaptured)
    {
        m_wPressed = false;
        m_sPressed = false;
        m_aPressed = false;
        m_dPressed = false;
        m_spacePressed = false;
        m_shiftPressed = false;
        return;
    }
	switch (key)
	{
	case SDLK_w:
		m_wPressed = true;
		break;
	case SDLK_s:
		m_sPressed = true;
		break;
	case SDLK_a:
		m_aPressed = true;
		break;
	case SDLK_d:
		m_dPressed = true;
		break;
	case SDLK_SPACE:
		m_spacePressed = true;
		break;
	case SDLK_LSHIFT:
		m_shiftPressed = true;
		break;
	default:
		break;
	}
}

void Camera::keyReleased(const uint32_t key)
{
	switch (key)
	{
	case SDLK_w:
		m_wPressed = false;
		break;
	case SDLK_s:
		m_sPressed = false;
		break;
	case SDLK_a:
		m_aPressed = false;
		break;
	case SDLK_d:
		m_dPressed = false;
		break;
	case SDLK_SPACE:
		m_spacePressed = false;
		break;
	case SDLK_LSHIFT:
		m_shiftPressed = false;
		break;
	default:
		break;
	}
}

void Camera::updateEvents(const float delta)
{
	glm::vec3 moveDir(0.0f);
	if (m_wPressed)
	{
		moveDir += m_Front;
	}
	if (m_sPressed)
	{
		moveDir -= m_Front;
	}
	if (m_aPressed)
	{
		moveDir -= m_right;
	}
	if (m_dPressed)
	{
		moveDir += m_right;
	}
	if (m_spacePressed)
	{
		moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);
	}
	if (m_shiftPressed)
	{
		moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
	}

	if (moveDir.x != 0.f || moveDir.y != 0.f || moveDir.z != 0.f)
	{
		move(glm::normalize(moveDir) * (m_movingSpeed * delta));
	}
}

glm::mat4& Camera::getFrustumVPMatrix()
{
    return m_FreezeFrustum ? m_FrozenVPMatrix : getVPMatrix();
}

void Camera::calculateRightVector()
{
	m_right = glm::normalize(glm::cross(m_Front, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void Camera::setMouseCaptured(const bool captured)
{
    m_isMouseCaptured = captured;
}

void Camera::mouseScrolled(const float y)
{
    m_movingSpeed = (1.f + y * 0.05f) * m_movingSpeed;
    m_movingSpeed = std::clamp(m_movingSpeed, 0.5f, 300.0f);
}

void Camera::setViewDirty()
{
    m_viewDirty = true;
    m_InvViewDirty = true;
    m_InvVPMatrixDirty = true;
}

void Camera::setProjDirty()
{
    m_projDirty = true;
    m_InvProjDirty = true;
    m_InvVPMatrixDirty = true;
}

void Camera::setFreezeFrustum(const bool freeze)
{
    if (freeze && !m_FreezeFrustum)
    {
        m_FrozenVPMatrix = getVPMatrix();
    }
    m_FreezeFrustum = freeze;
}
