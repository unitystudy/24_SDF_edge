Shader "Unlit/SDF"
{
    Properties
    {
		_MainTex("Texture", 2D) = "white" {}
		_SDF("SDF", 2D) = "white" {}
		_EdgeColor("Edge", Color) = (.34, .85, .92, 1)
		_EdgeMin("Min Edge Value", Range(-1.0,1.0)) = 0.0
		_EdgeMax("Max Edge Value", Range(-1.0,1.0)) = 0.1
	}
    SubShader
    {
        Tags { "RenderType"="Transparent"  "Queue" = "Transparent"}
		Blend SrcAlpha OneMinusSrcAlpha
        LOD 100

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag

            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct v2f
            {
                float2 uv : TEXCOORD0;
                float4 vertex : SV_POSITION;
            };

			sampler2D _MainTex;
			float4 _MainTex_ST;
			sampler2D _SDF;
			fixed4 _EdgeColor;
			float _EdgeMin;
			float _EdgeMax;

            v2f vert (appdata v)
            {
                v2f o;
                o.vertex = UnityObjectToClipPos(v.vertex);
                o.uv = TRANSFORM_TEX(v.uv, _MainTex);
                return o;
            }

            fixed4 frag (v2f i) : SV_Target
            {
				fixed4 col = tex2D(_MainTex, i.uv);
				float SDF = tex2D(_SDF, i.uv).r * 2.0 - 1.0;
				col = (_EdgeMin < SDF && SDF < _EdgeMax) ? _EdgeColor : col;
				return col;
            }
            ENDCG
        }
    }
}
