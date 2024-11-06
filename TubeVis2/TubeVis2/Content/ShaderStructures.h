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

	/*struct matrices_and_user_input {
		/// <summary>
		/// The view matrix given by the quake cam
		/// </summary>
		DirectX::XMFLOAT4X4 mViewMatrix;
		/// <summary>
		/// The projection matrix given by the quake cam
		/// </summary>
		DirectX::XMFLOAT4X4 mProjMatrix;
		/// <summary>
		/// The position of the camera/eye in world space
		/// </summary>
		DirectX::XMFLOAT4 mCamPos;
		/// <summary>
		/// The looking direction of the camera/eye in world space
		/// </summary>
		DirectX::XMFLOAT4 mCamDir;
		/// <summary>
		/// rgb ... The background color for the background shader
		/// a   ... The strength of the gradient
		/// </summary>
		DirectX::XMFLOAT4 mClearColor;
		/// <summary>
		/// rgb ... The color for the 2d helper lines.
		/// a   ... ignored
		/// </summary>
		DirectX::XMFLOAT4 mHelperLineColor;
		/// <summary>
		/// contains resx, resy and kbuffer levels
		/// </summary>
		DirectX::XMFLOAT4 mkBufferInfo;

		/// <summary>
		/// The direction of the light/sun in WS
		/// </summary>
		DirectX::XMFLOAT4 mDirLightDirection;
		/// <summary>
		/// The color of the light/sun multiplied with the intensity
		/// a   ... ignored
		/// </summary>
		DirectX::XMFLOAT4 mDirLightColor;
		/// <summary>
		/// The color of the ambient light
		/// a   ... ignored
		/// </summary>
		DirectX::XMFLOAT4 mAmbLightColor;
		/// <summary>
		/// The material light properties for the tubes:
		/// r ... ambient light factor
		/// g ... diffuse light factor
		/// b ... specular light factor
		/// a ... shininess
		/// </summary>
		DirectX::XMFLOAT4 mMaterialLightReponse; // vec4(0.5, 1.0, 0.5, 32.0);  // amb, diff, spec, shininess

		/// <summary>
		/// The vertex color for minimum values (depending on the mode).
		/// Is also used for the color if in static mode
		/// a ... ignored
		/// </summary>
		DirectX::XMFLOAT4 mVertexColorMin;
		/// <summary>
		/// The vertex color for vertices with maximum values (depending on the mode)
		/// a ... ignored
		/// </summary>
		DirectX::XMFLOAT4 mVertexColorMax;
		/// <summary>
		/// The min/max levels for line transparencies in dynamic modes
		/// The min value is also used if in static mode
		/// ba ... ignored
		/// </summary>
		DirectX::XMFLOAT4 mVertexAlphaBounds;
		/// <summary>
		/// The min/max level for the radius of vertices in dynamic modes
		/// The min value is also used if in static mode
		/// ba ... ignored
		/// </summary>
		DirectX::XMFLOAT4 mVertexRadiusBounds;

		/// <summary>
		/// Flag to enable/disable the clipping of the billboard based on the raycasting
		/// and for the caps.
		/// </summary>
		BOOL mBillboardClippingEnabled;
		/// <summary>
		/// Flag to enable/disable shading of the billboard (raycasting will still be done for possible clipping)
		/// </summary>
		BOOL mBillboardShadingEnabled;

		/// <summary>
		/// The coloring mode for vertices (see main->renderUI() for possible states)
		/// </summary>
		uint32_t mVertexColorMode;
		/// <summary>
		/// The transparency mode for vertices (see main->renderUI() for possible states)
		/// </summary>
		uint32_t mVertexAlphaMode;
		/// <summary>
		/// The radius mode for vertices (see main->renderUI() for possible states)
		/// </summary>
		uint32_t mVertexRadiusMode;

		/// <summary>
		/// Reverses the factor (depending on the mode) for dynamic transparency if true
		/// </summary>
		BOOL mVertexAlphaInvert;
		/// <summary>
		/// Reverses the factor (depending on the mode) for dynamic radius if true
		/// </summary>
		BOOL mVertexRadiusInvert;
		/// <summary>
		/// The maximum line length inside the dataset. Which is necessary to calculate a
		/// factor from 0-1 in the depending on line length mode.
		/// </summary>
		float mDataMaxLineLength;
		/// <summary>
		/// The maximum line length of adjacing lines to a vertex inside the dataset.
		/// This value is unused so far but could be used for another dynamic mode
		/// </summary>
		float mDataMaxVertexAdjacentLineLength;

	};*/

	struct matrices_and_user_input {
		/// <summary>
		/// contains resx, resy and kbuffer levels
		/// </summary>
		DirectX::XMFLOAT4 mkBufferInfo;
	};
}