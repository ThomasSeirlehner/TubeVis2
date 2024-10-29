#include "pch.h"
#include "TubeVis2Main.h"
#include "Common\DirectXHelper.h"

using namespace TubeVis2;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Concurrency;

// Die Vorlage "DirectX 12-Anwendung" wird unter https://go.microsoft.com/fwlink/?LinkID=613670&clcid=0x407 dokumentiert.

// L�dt und initialisiert die Anwendungsobjekte, wenn die Anwendung geladen wird.
TubeVis2Main::TubeVis2Main()
{
	// TODO: Timereinstellungen �ndern, wenn Sie etwas Anderes m�chten als den standardm��igen variablen Zeitschrittmodus.
	// z.�B. f�r eine Aktualisierungslogik mit festen 60�FPS-Zeitschritten Folgendes aufrufen:
	/*
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
	*/
}

// Erstellt und initialisiert die Renderer.
void TubeVis2Main::CreateRenderers(const std::shared_ptr<DX::DeviceResources>& deviceResources)
{
	// TODO: Dies durch Ihre App-Inhaltsinitialisierung ersetzen.
	m_sceneRenderer = std::unique_ptr<Sample3DSceneRenderer>(new Sample3DSceneRenderer(deviceResources));

	OnWindowSizeChanged();
}

// Aktualisiert den Anwendungszustand ein Mal pro Frame.
void TubeVis2Main::Update()
{
	// Die Szeneobjekte aktualisieren.
	m_timer.Tick([&]()
	{
		// TODO: Dies durch Ihre App-Inhaltsupdatefunktionen ersetzen.
		m_sceneRenderer->Update(m_timer);
	});
}

// Rendert den aktuellen Frame dem aktuellen Anwendungszustand entsprechend.
// Gibt True zur�ck, wenn der Frame gerendert wurde und angezeigt werden kann.
bool TubeVis2Main::Render()
{
	// Nicht versuchen, etwas vor dem ersten Update zu rendern.
	if (m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	// Die Szeneobjekte rendern.
	// TODO: Dies durch die Inhaltsrenderingfunktionen Ihrer App ersetzen.
	return m_sceneRenderer->Render();
}

// Aktualisiert den Anwendungszustand, wenn sich die Fenstergr��e �ndert (z. B. �nderung der Ger�teausrichtung)
void TubeVis2Main::OnWindowSizeChanged()
{
	// TODO: Dies durch Ihre gr��enabh�ngige Initialisierung Ihres App-Inhalts ersetzen.
	m_sceneRenderer->CreateWindowSizeDependentResources();
}

// Benachrichtigt die App, dass sie gesperrt wird.
void TubeVis2Main::OnSuspending()
{
	// TODO: Dies durch Ihre App-Sperrlogik ersetzen.

	// Die Prozesslebensdauer-Verwaltung kann gesperrte Apps jederzeit beenden, daher ist es
	// eine bew�hrte Vorgehensweise, jeden Zustand zu speichern, der es erm�glicht, die App an dem Punkt neu zu starten, an dem sie beendet wurde.

	m_sceneRenderer->SaveState();

	// Wenn die Anwendung Videospeicherbelegungen nutzt, die einfach neu erstellt werden k�nnen,
	// sollten Sie in Erw�gung ziehen, diesen Arbeitsspeicher freizugeben, um ihn anderen Anwendungen zur Verf�gung zu stellen.
}

// Benachrichtigt die App, dass sie nicht l�nger gesperrt ist.
void TubeVis2Main::OnResuming()
{
	// TODO: Dies durch Ihre App-Fortsetzungslogik ersetzen.
}

// Weist Renderer darauf hin, dass die Ger�teressourcen freigegeben werden m�ssen.
void TubeVis2Main::OnDeviceRemoved()
{
	// TODO: Jeden erforderlichen Anwendungs- oder Rendererzustand speichern und den Renderer freigeben.
	// und die Ressourcen, die nicht l�nger g�ltig sind.
	m_sceneRenderer->SaveState();
	m_sceneRenderer = nullptr;
}
