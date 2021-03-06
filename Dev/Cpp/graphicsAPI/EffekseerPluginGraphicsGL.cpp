﻿
#include "EffekseerPluginGraphicsGL.h"
#include <algorithm>
#include <assert.h>

#ifdef __EFFEKSEER_FROM_MAIN_CMAKE__
#else
#include <EffekseerRenderer/EffekseerRendererGL.MaterialLoader.h>
#include <EffekseerRenderer/EffekseerRendererGL.ModelLoader.h>
#endif

#include "../common/EffekseerPluginMaterial.h"

namespace EffekseerPlugin
{

class TextureLoaderGL : public TextureLoader
{
	struct TextureResource
	{
		int referenceCount = 1;
		Effekseer::TextureData* textureDataPtr = nullptr;
	};

	std::map<std::u16string, TextureResource> resources;
	std::map<void*, void*> textureData2NativePtr;
	UnityGfxRenderer gfxRenderer;

public:
	TextureLoaderGL(TextureLoaderLoad load, TextureLoaderUnload unload, UnityGfxRenderer renderer);

	virtual ~TextureLoaderGL();

	virtual Effekseer::TextureData* Load(const EFK_CHAR* path, Effekseer::TextureType textureType);

	virtual void Unload(Effekseer::TextureData* source);
};

bool IsPowerOfTwo(uint32_t x) { return (x & (x - 1)) == 0; }

TextureLoaderGL::TextureLoaderGL(TextureLoaderLoad load, TextureLoaderUnload unload, UnityGfxRenderer renderer)
	: TextureLoader(load, unload), gfxRenderer(renderer)
{
}

TextureLoaderGL::~TextureLoaderGL() {}

Effekseer::TextureData* TextureLoaderGL::Load(const EFK_CHAR* path, Effekseer::TextureType textureType)
{
	// リソーステーブルを検索して存在したらそれを使う
	auto it = resources.find((const char16_t*)path);
	if (it != resources.end())
	{
		it->second.referenceCount++;
		return it->second.textureDataPtr;
	}

	// Unityでテクスチャをロード
	int32_t width, height, format;
	int64_t textureID = reinterpret_cast<int64_t>(load((const char16_t*)path, &width, &height, &format));
	if (textureID == 0)
	{
		return nullptr;
	}
	// リソーステーブルに追加
	auto added = resources.insert(std::make_pair((const char16_t*)path, TextureResource()));
	TextureResource& res = added.first->second;
	res.textureDataPtr = new Effekseer::TextureData();
	res.textureDataPtr->Width = width;
	res.textureDataPtr->Height = height;
	res.textureDataPtr->TextureFormat = (Effekseer::TextureFormatType)format;

	res.textureDataPtr->UserID = textureID;
#if !defined(_WIN32)
	if (gfxRenderer != kUnityGfxRendererOpenGLES20 || (IsPowerOfTwo(res.textureDataPtr->Width) && IsPowerOfTwo(res.textureDataPtr->Height)))
	{
		// テクスチャのミップマップを生成する
		glBindTexture(GL_TEXTURE_2D, (GLuint)textureID);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
#endif

	textureData2NativePtr[res.textureDataPtr] = (void*)textureID;

	return res.textureDataPtr;
}

void TextureLoaderGL::Unload(Effekseer::TextureData* source)
{
	if (source == nullptr)
	{
		return;
	}

	// アンロードするテクスチャを検索
	auto it = std::find_if(resources.begin(), resources.end(), [source](const std::pair<std::u16string, TextureResource>& pair) {
		return pair.second.textureDataPtr->UserID == source->UserID;
	});
	if (it == resources.end())
	{
		return;
	}

	// 参照カウンタが0になったら実際にアンロード
	it->second.referenceCount--;
	if (it->second.referenceCount <= 0)
	{
		// Unity側でアンロード
		unload(it->first.c_str(), textureData2NativePtr[source]);
		textureData2NativePtr.erase(source);
		ES_SAFE_DELETE(it->second.textureDataPtr);
		resources.erase(it);
	}
}

GraphicsGL::GraphicsGL(UnityGfxRenderer renderer) : gfxRenderer(renderer)
{
	deviceObjectCollection_ = new ::EffekseerRendererGL::DeviceObjectCollection();

	MaterialEvent::Initialize();
}

GraphicsGL::~GraphicsGL() {}

bool GraphicsGL::Initialize(IUnityInterfaces* unityInterfaces)
{

	switch (gfxRenderer)
	{
	case kUnityGfxRendererOpenGL:
		openglDeviceType = EffekseerRendererGL::OpenGLDeviceType::OpenGL2;
		break;
	case kUnityGfxRendererOpenGLES20:
		openglDeviceType = EffekseerRendererGL::OpenGLDeviceType::OpenGLES2;
		break;
	case kUnityGfxRendererOpenGLES30:
		openglDeviceType = EffekseerRendererGL::OpenGLDeviceType::OpenGLES3;
		break;
	case kUnityGfxRendererOpenGLCore:
		openglDeviceType = EffekseerRendererGL::OpenGLDeviceType::OpenGL3;
		break;
	}

	return true;
}

void GraphicsGL::Shutdown(IUnityInterfaces* unityInterface)
{
	renderer_ = nullptr;
	ES_SAFE_RELEASE(deviceObjectCollection_);

	MaterialEvent::Terminate();
}

EffekseerRenderer::Renderer* GraphicsGL::CreateRenderer(int squareMaxCount, bool reversedDepth)
{
#ifdef __ANDROID__
	squareMaxCount = 600;
#endif
	auto renderer = EffekseerRendererGL::Renderer::Create(squareMaxCount, openglDeviceType, deviceObjectCollection_);
	renderer_ = renderer;
	return renderer;
}

void GraphicsGL::SetBackGroundTextureToRenderer(EffekseerRenderer::Renderer* renderer, void* backgroundTexture)
{
	((EffekseerRendererGL::Renderer*)renderer)->SetBackground((GLuint)(uintptr_t)backgroundTexture);
}

void GraphicsGL::EffekseerSetBackGroundTexture(int renderId, void* texture) { renderSettings[renderId].backgroundTexture = texture; }

Effekseer::TextureLoader* GraphicsGL::Create(TextureLoaderLoad load, TextureLoaderUnload unload)
{
	return new TextureLoaderGL(load, unload, gfxRenderer);
}

Effekseer::ModelLoader* GraphicsGL::Create(ModelLoaderLoad load, ModelLoaderUnload unload)
{
	auto loader = new ModelLoader(load, unload);

#ifdef __EFFEKSEER_FROM_MAIN_CMAKE__
	auto internalLoader = EffekseerRendererGL::CreateModelLoader(loader->GetFileInterface());
#else
	auto internalLoader = new EffekseerRendererGL::ModelLoader(loader->GetFileInterface());
#endif
	loader->SetInternalLoader(internalLoader);
	return loader;
}

Effekseer::MaterialLoader* GraphicsGL::Create(MaterialLoaderLoad load, MaterialLoaderUnload unload)
{
	auto loader = new MaterialLoader(load, unload);
#ifdef __EFFEKSEER_FROM_MAIN_CMAKE__
	auto internalLoader = renderer_->CreateMaterialLoader();
#else
	auto internalLoader = new ::EffekseerRendererGL::MaterialLoader(openglDeviceType, nullptr, deviceObjectCollection_, nullptr);
#endif
	auto holder = std::make_shared<MaterialLoaderHolder>(internalLoader);
	loader->SetInternalLoader(holder);
	return loader;
}

void GraphicsGL::ShiftViewportForStereoSinglePass(bool isShift)
{
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	if (isShift)
		viewport[0] = viewport[2];
	else
		viewport[0] = 0;
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

} // namespace EffekseerPlugin