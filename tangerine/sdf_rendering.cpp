
// Copyright 2023 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sdf_rendering.h"
#include "sdf_model.h"
#include "profiling.h"
#include <iostream>


struct TransformUpload
{
	glm::mat4 LocalToWorld;
	glm::mat4 WorldToLocal;
};


void SodapopDrawable::Draw(
	glm::vec3 CameraOrigin,
	SDFModel* Instance)
{
	if (!glm::all(glm::equal(Instance->CameraOrigin, CameraOrigin)))
	{
		Instance->CameraOrigin = CameraOrigin;
		Instance->Dirty.store(true);
	}

	if (Instance->Visibility == VisibilityStates::Imminent || !MeshReady.load())
	{
		return;
	}

	Instance->Drawing.store(true);
	Instance->SodapopCS.lock();

	if (!MeshUploaded)
	{
		IndexBuffer.Upload(Indices.data(), Indices.size() * sizeof(uint32_t));
		PositionBuffer.Upload(Positions.data(), Positions.size() * sizeof(glm::vec4));

		MeshUploaded = true;
	}
	{
		if (Instance->Colors.size() > 0)
		{
			Instance->ColorBuffer.Upload(Instance->Colors.data(), Instance->Colors.size() * sizeof(glm::vec4));

			IndexBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
			PositionBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 3);
			Instance->ColorBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 4);

			glDrawArrays(GL_TRIANGLES, 0, Indices.size());
		}
	}

	Instance->Drawing.store(false);
	Instance->SodapopCS.unlock();
}


void SodapopDrawable::Draw(
	glm::vec3 CameraOrigin,
	const int PositionBinding,
	const int ColorBinding,
	SDFModel* Instance)
{
	if (!glm::all(glm::equal(Instance->CameraOrigin, CameraOrigin)))
	{
		Instance->CameraOrigin = CameraOrigin;
		Instance->Dirty.store(true);
	}

	if (Instance->Visibility == VisibilityStates::Imminent || !MeshReady.load())
	{
		return;
	}

	Instance->Drawing.store(true);
	Instance->SodapopCS.lock();

	if (!MeshUploaded)
	{
		IndexBuffer.Upload(GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, Indices.data(), Indices.size() * sizeof(uint32_t));
		PositionBuffer.Upload(GL_ARRAY_BUFFER, GL_STATIC_DRAW, Positions.data(), Positions.size() * sizeof(glm::vec4));

		MeshUploaded = true;
	}
	{
		if (Instance->Colors.size() > 0)
		{
			IndexBuffer.Bind(GL_ELEMENT_ARRAY_BUFFER);

			PositionBuffer.Bind(GL_ARRAY_BUFFER);
			glVertexAttribPointer(PositionBinding, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

			Instance->ColorBuffer.Upload(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, Instance->Colors.data(), Instance->Colors.size() * sizeof(glm::vec4));
			Instance->ColorBuffer.Bind(GL_ARRAY_BUFFER);
			glVertexAttribPointer(ColorBinding, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

			glDrawElements(GL_TRIANGLES, Indices.size(), GL_UNSIGNED_INT, nullptr);
		}
	}

	Instance->Drawing.store(false);
	Instance->SodapopCS.unlock();
}


void SDFModel::Draw(
	glm::vec3 CameraOrigin)
{
	if (Painter && Visibility != VisibilityStates::Invisible)
	{
		Transform.Fold();
		ThreadSafeTransform.store(Transform.LastFoldInverse);

		if (Visibility == VisibilityStates::Visible)
		{
			TransformUpload TransformData = {
				Transform.LastFold,
				Transform.LastFoldInverse
			};
			TransformBuffer.Upload((void*)&TransformData, sizeof(TransformUpload));
			TransformBuffer.Bind(GL_UNIFORM_BUFFER, 1);
		}

		Painter->Draw(CameraOrigin, this);
	}
}


void SDFModel::Draw(
	glm::vec3 CameraOrigin,
	const int LocalToWorldBinding,
	const int PositionBinding,
	const int ColorBinding)
{
	if (Painter && Visibility != VisibilityStates::Invisible)
	{
		Transform.Fold();
		ThreadSafeTransform.store(Transform.LastFoldInverse);

		if (Visibility == VisibilityStates::Visible)
		{
			glUniformMatrix4fv(LocalToWorldBinding, 1, false, (const GLfloat*)(&Transform.LastFold));
		}

		Painter->Draw(CameraOrigin, PositionBinding, ColorBinding, this);
	}
}
