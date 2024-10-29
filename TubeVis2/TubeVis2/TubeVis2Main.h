#pragma once

#include "Common\StepTimer.h"
#include "Common\DeviceResources.h"
#include "Content\Sample3DSceneRenderer.h"

// Rendert Direct3D-Inhalte auf dem Bildschirm.
namespace TubeVis2
{
	class TubeVis2Main
	{
	public:
		TubeVis2Main();
		void CreateRenderers(const std::shared_ptr<DX::DeviceResources>& deviceResources);
		void Update();
		bool Render();

		void OnWindowSizeChanged();
		void OnSuspending();
		void OnResuming();
		void OnDeviceRemoved();

	private:
		// TODO: Durch Ihre eigenen Inhaltsrenderer ersetzen.
		std::unique_ptr<Sample3DSceneRenderer> m_sceneRenderer;

		// Schleifentimer wird gerendert.
		DX::StepTimer m_timer;
	};
}