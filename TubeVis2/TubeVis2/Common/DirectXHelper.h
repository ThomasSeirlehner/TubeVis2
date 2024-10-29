#pragma once

#include <ppltasks.h>	// Für create_task

namespace DX
{
	inline void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr))
		{
			// In dieser Zeile einen Haltepunkt festlegen, um Fehler der Win32-API abzufangen.
			throw Platform::Exception::CreateException(hr);
		}
	}

	// Funktion, die asynchron aus einer Binärdatei liest.
	inline Concurrency::task<std::vector<byte>> ReadDataAsync(const std::wstring& filename)
	{
		using namespace Windows::Storage;
		using namespace Concurrency;

		auto folder = Windows::ApplicationModel::Package::Current->InstalledLocation;

		return create_task(folder->GetFileAsync(Platform::StringReference(filename.c_str()))).then([](StorageFile^ file)
		{
			return FileIO::ReadBufferAsync(file);
		}).then([](Streams::IBuffer^ fileBuffer) -> std::vector<byte>
		{
			std::vector<byte> returnBuffer;
			returnBuffer.resize(fileBuffer->Length);
			Streams::DataReader::FromBuffer(fileBuffer)->ReadBytes(Platform::ArrayReference<byte>(returnBuffer.data(), fileBuffer->Length));
			return returnBuffer;
		});
	}

	// Wandelt eine Längenangabe in geräteunabhängigen Pixeln (Device-Independent Pixels, DIPs) in eine Längenangabe in physischen Pixeln um.
	inline float ConvertDipsToPixels(float dips, float dpi)
	{
		static const float dipsPerInch = 96.0f;
		return floorf(dips * dpi / dipsPerInch + 0.5f); // Auf nächste ganze Zahl runden.
	}

	// Weist dem Objekt einen Namen zu, um das Debuggen unterstützen.
#if defined(_DEBUG)
	inline void SetName(ID3D12Object* pObject, LPCWSTR name)
	{
		pObject->SetName(name);
	}
#else
	inline void SetName(ID3D12Object*, LPCWSTR)
	{
	}
#endif
}

// Die Benennungshilfsfunktion für "ComPtr<T>".
// Weist den Namen der Variablen als Namen des Objekts zu.
#define NAME_D3D12_OBJECT(x) DX::SetName(x.Get(), L#x)
