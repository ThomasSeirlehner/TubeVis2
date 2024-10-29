#pragma once

namespace TubeVis2
{
	// Konstantenpuffer zum Senden von MVP-Matrizen an den Vertex-Shader verwendet.
	struct ModelViewProjectionConstantBuffer
	{
		DirectX::XMFLOAT4X4 model;
		DirectX::XMFLOAT4X4 view;
		DirectX::XMFLOAT4X4 projection;
	};

	// Verwendet zum Senden von Pro-Vertex-Daten an den Vertex-Shader.
	struct VertexPositionColor
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 color;
	};
}