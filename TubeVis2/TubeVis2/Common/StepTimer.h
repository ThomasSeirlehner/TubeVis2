#pragma once

#include <wrl.h>

namespace DX
{
	// Hilfsklasse für die zeitliche Steuerung von Animation und Simulation.
	class StepTimer
	{
	public:
		StepTimer() : 
			m_elapsedTicks(0),
			m_totalTicks(0),
			m_leftOverTicks(0),
			m_frameCount(0),
			m_framesPerSecond(0),
			m_framesThisSecond(0),
			m_qpcSecondCounter(0),
			m_isFixedTimeStep(false),
			m_targetElapsedTicks(TicksPerSecond / 60)
		{
			if (!QueryPerformanceFrequency(&m_qpcFrequency))
			{
				throw ref new Platform::FailureException();
			}

			if (!QueryPerformanceCounter(&m_qpcLastTime))
			{
				throw ref new Platform::FailureException();
			}

			// Max. Differenz auf 1/10 einer Sekunde initialisieren.
			m_qpcMaxDelta = m_qpcFrequency.QuadPart / 10;
		}

		// Verstrichene Zeit seit dem vorherigen Update-Aufruf.
		uint64 GetElapsedTicks() const						{ return m_elapsedTicks; }
		double GetElapsedSeconds() const					{ return TicksToSeconds(m_elapsedTicks); }

		// Gesamtzeit seit dem Programmstart abrufen.
		uint64 GetTotalTicks() const						{ return m_totalTicks; }
		double GetTotalSeconds() const						{ return TicksToSeconds(m_totalTicks); }

		// Gesamtzahl an Aktualisierungen seit dem Programmstart abrufen.
		uint32 GetFrameCount() const						{ return m_frameCount; }

		// Die aktuelle Framerate abrufen.
		uint32 GetFramesPerSecond() const					{ return m_framesPerSecond; }

		// Festlegen, ob der feste oder variable Zeitschrittmodus verwendet wird.
		void SetFixedTimeStep(bool isFixedTimestep)			{ m_isFixedTimeStep = isFixedTimestep; }

		// Im festen Zeitschrittmodus festlegen, wie oft Update aufgerufen werden soll.
		void SetTargetElapsedTicks(uint64 targetElapsed)	{ m_targetElapsedTicks = targetElapsed; }
		void SetTargetElapsedSeconds(double targetElapsed)	{ m_targetElapsedTicks = SecondsToTicks(targetElapsed); }

		// Ganzzahliges Format stellt die Zeit in 10.000.000 Takten pro Sekunde dar.
		static const uint64 TicksPerSecond = 10000000;

		static double TicksToSeconds(uint64 ticks)			{ return static_cast<double>(ticks) / TicksPerSecond; }
		static uint64 SecondsToTicks(double seconds)		{ return static_cast<uint64>(seconds * TicksPerSecond); }

		// Nach einer absichtlichen Zeitsteuerungsdiskontinuität (z. B. ein blockierender EA-Vorgang)
		// Dies aufrufen, um zu vermeiden, dass die feste Zeitschrittlogik versucht, einen Satz von aufholenden 
		// Aktualisierungsaufrufe.

		void ResetElapsedTime()
		{
			if (!QueryPerformanceCounter(&m_qpcLastTime))
			{
				throw ref new Platform::FailureException();
			}

			m_leftOverTicks = 0;
			m_framesPerSecond = 0;
			m_framesThisSecond = 0;
			m_qpcSecondCounter = 0;
		}

		// Timerzustand aktualisieren, dabei die angegebene Aktualisierungsfunktion entsprechend häufig aufrufen.
		template<typename TUpdate>
		void Tick(const TUpdate& update)
		{
			// Die aktuelle Uhrzeit abfragen.
			LARGE_INTEGER currentTime;

			if (!QueryPerformanceCounter(&currentTime))
			{
				throw ref new Platform::FailureException();
			}

			uint64 timeDelta = currentTime.QuadPart - m_qpcLastTime.QuadPart;

			m_qpcLastTime = currentTime;
			m_qpcSecondCounter += timeDelta;

			// Ungewöhnlich große Zeitunterschiede binden (z. B. nach Anhalten im Debugger).
			if (timeDelta > m_qpcMaxDelta)
			{
				timeDelta = m_qpcMaxDelta;
			}

			// QPC-Einheiten in ein kanonisches Taktformat umwandeln. Ein Überlaufen ist aufgrund des vorherigen Clamps nicht möglich.
			timeDelta *= TicksPerSecond;
			timeDelta /= m_qpcFrequency.QuadPart;

			uint32 lastFrameCount = m_frameCount;

			if (m_isFixedTimeStep)
			{
				// Aktualisierungslogik mit festen Zeitschritten

				// Wenn die App sehr nahe an der anvisierten verstrichenen Zeit ausgeführt wird (innerhalb einer 1/4 Millisekunde), einfach binden
				// Uhr so einstellen, dass sie genau dem Zielwert entspricht. Dadurch werden winzige und irrelevante Fehler verhindert.
				// dass sie sich im Zeitverlauf sammeln. Ohne dieses Anbinden würde ein Spiel, dass eine 60 FPS
				// feste Aktualisierung, ausgeführt mit Vsync auf einem 59.94 NTSC-Display aktiviert, würde schließlich
				// sammelt genug winzige Fehler, dass dadurch ein Frame abgelegt würde. Besser wäre ein Runden 
				// kleine Abweichungen auf null, um die Dinge ruhig laufen zu lassen.

				if (abs(static_cast<int64>(timeDelta - m_targetElapsedTicks)) < TicksPerSecond / 4000)
				{
					timeDelta = m_targetElapsedTicks;
				}

				m_leftOverTicks += timeDelta;

				while (m_leftOverTicks >= m_targetElapsedTicks)
				{
					m_elapsedTicks = m_targetElapsedTicks;
					m_totalTicks += m_targetElapsedTicks;
					m_leftOverTicks -= m_targetElapsedTicks;
					m_frameCount++;

					update();
				}
			}
			else
			{
				// Aktualisierungslogik mit variablen Zeitschritten.
				m_elapsedTicks = timeDelta;
				m_totalTicks += timeDelta;
				m_leftOverTicks = 0;
				m_frameCount++;

				update();
			}

			// Die aktuelle Framerate nachverfolgen.
			if (m_frameCount != lastFrameCount)
			{
				m_framesThisSecond++;
			}

			if (m_qpcSecondCounter >= static_cast<uint64>(m_qpcFrequency.QuadPart))
			{
				m_framesPerSecond = m_framesThisSecond;
				m_framesThisSecond = 0;
				m_qpcSecondCounter %= m_qpcFrequency.QuadPart;
			}
		}

	private:
		// Quellzeiterfassungsdaten verwenden QPC-Einheiten.
		LARGE_INTEGER m_qpcFrequency;
		LARGE_INTEGER m_qpcLastTime;
		uint64 m_qpcMaxDelta;

		// Abgeleitete Zeitsteuerungsdaten verwenden ein kanonisches Taktformat.
		uint64 m_elapsedTicks;
		uint64 m_totalTicks;
		uint64 m_leftOverTicks;

		// Member zur Nachverfolgung der Framerate.
		uint32 m_frameCount;
		uint32 m_framesPerSecond;
		uint32 m_framesThisSecond;
		uint64 m_qpcSecondCounter;

		// Member zum Konfigurieren des festen Zeitschrittmodus.
		bool m_isFixedTimeStep;
		uint64 m_targetElapsedTicks;
	};
}
