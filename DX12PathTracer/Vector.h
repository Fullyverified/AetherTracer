#pragma once

#include <numbers>
#include <iostream>

namespace PT	{

	struct Vector4 {
		float x, y, z, w;
		Vector4() : x(0), y(0), z(0), w(0) {};
		Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {};
	};

	struct Vector3 {
		float x, y, z;
		Vector3() : x(0), y(0), z(0) {};
		Vector3(float x, float y, float z) : x(x), y(y), z(z) {};
	};

	struct Vector2 {
		float x, y;
		Vector2() : x(0), y(0) {};
		Vector2(float x, float y) : x(x), y(y) {};
	};

	static void Print(Vector3& vec) {
		std::cout << "X: " << vec.x << ", Y: " << vec.y << ", Z: " << vec.z << std::endl;
	}

	static Vector2 Normalize(const Vector2& vec) {
		float lengthSq = vec.x * vec.x + vec.y * vec.y;
		float length = sqrtf(lengthSq);
		float inv = 1.0f / length;

		return { vec.x * inv, vec.y * inv };
	}

	static Vector3 Normalize(const Vector3& vec) {
		float lengthSQ = vec.x * vec.x + vec.y * vec.y + vec.z * vec.z;
		float length = sqrtf(lengthSQ);
		float inv = 1.0f / length;
		return { vec.x * inv, vec.y * inv, vec.z * inv };
	}

	static Vector3 FromEuler(const Vector2& vec) {
		float yaw = vec.x;
		float pitch = vec.y;

		float yawRad = (static_cast<float>(std::numbers::pi) * yaw) / 180.0f;
		float pitchRad = (static_cast<float>(std::numbers::pi) * pitch) / 180.0f;

		Vector3 forward;

		forward.x = cosf(yawRad) * cosf(pitchRad);
		forward.y = sinf(pitchRad);
		forward.z = -sinf(yawRad) * cosf(pitchRad); // -z forward

		std::cout << "Un-normalized Forward:";
		Print(forward);
		return Normalize(forward);
	}

	static Vector3 Cross(const Vector3& vec, const Vector3& other) {

		float x = vec.y * other.z - vec.z * other.y;
		float y = vec.z * other.x - vec.x * other.z;
		float z = vec.x * other.y - vec.y * other.x;

		return { x, y, z };
	}

	static float toRadians(float deg) {
	
		return (deg * static_cast<float>(std::numbers::pi)) / 180.0f;
	}
}

