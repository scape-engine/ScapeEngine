$input v_viewDir
#include "../common/common.sh"

// --- Uniforms ---
uniform vec4 u_parameters;         // x=sun radius, y=bloom, z=exposure, w=time
uniform vec4 u_sunDirection;       // xyz = sun direction
uniform vec4 u_sunLuminance;       // rgb = sun color * intensity
uniform vec4 u_scatterParams;      // x = mie anisotropy (g)
uniform vec4 u_betaR;              // rgb = Rayleigh coeff
uniform vec4 u_betaM;              // rgb = Mie coeff
uniform vec4 u_artisticInfluence;  // Unused

// --- Constants ---
#define PI 3.14159265359
#define SAMPLES 8 // Raymarching steps for the atmosphere

// --- Noise Helpers ---
// Generates a pseudo-random number between 0 and 1 based on a 2D position.
float hash(vec2 p) {
    p = frac(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

// 3D Noise generator for volumetric effects (clouds/galaxy).
// Interpolates random values across a 3D grid.
float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f); // Hermite smoothing
    float n = i.x + i.y * 57.0 + i.z * 113.0;
    
    return lerp(lerp(lerp(frac(sin(n + 0.0) * 43758.5453), frac(sin(n + 1.0) * 43758.5453), f.x),
                   lerp(frac(sin(n + 57.0) * 43758.5453), frac(sin(n + 58.0) * 43758.5453), f.x), f.y),
               lerp(lerp(frac(sin(n + 113.0) * 43758.5453), frac(sin(n + 114.0) * 43758.5453), f.x),
                   lerp(frac(sin(n + 170.0) * 43758.5453), frac(sin(n + 171.0) * 43758.5453), f.x), f.y), f.z);
}

// Fractal Brownian Motion (FBM function)
float fbm(vec3 p) {
    float f = 0.0;
    float amp = 0.5;
    for(int i = 0; i < 4; i++) {
        f += amp * noise3D(p);
        p *= 2.02; // Shift frequency slightly to avoid artifacts
        amp *= 0.5;
    }
    return f;
}

// --- ACES Tonemapping (Academy Color Encoding System) ---

vec3 ACESTonemap(vec3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// --- Ray-Sphere Intersection ---
// Calculates WHERE the camera ray enters and exits a sphere.
// Returns: vec2(t_near, t_far). t_far is the distance to the exit point.
vec2 raySphereIntersect(vec3 r0, vec3 rd, float sr) {
    float a = dot(rd, rd);
    float b = 2.0 * dot(rd, r0);
    float c = dot(r0, r0) - (sr * sr);
    float d = (b * b) - 4.0 * a * c;
    if (d < 0.0) return vec2_splat(-1.0);
    return vec2((-b - sqrt(d)) / (2.0 * a), (-b + sqrt(d)) / (2.0 * a));
}

// --- PHYSICS HELPER: TRANSMITTANCE ---
// Calculates how much light survives passing through the atmosphere
// based on the angle (zenith). Uses a simplified Chapman function approx.
// Used to turn the sun red at the horizon.
vec3 getTransmittance(float zenithCos, vec3 betaR, vec3 betaM) {
    // Distance to edge of atmosphere along this angle
    // Scale height approx 8km.
    float pathLength = 8000.0 / max(zenithCos, 0.05);
    
    // Beer's Law: exp(-OpticalDepth)
    return exp(-(betaR + betaM) * pathLength);
}

void main() {
    vec3 viewDir = normalize(v_viewDir);
    vec3 sunDir = normalize(u_sunDirection.xyz);
    
    // --- 1. Atmosphere Setup ---
    const float earthRadius = 6360000.0;
    const float atmosRadius = 6420000.0;
    vec3 sunLight = u_sunLuminance.rgb * u_sunLuminance.a;

    vec3 rayStart = vec3(0.0, earthRadius + 1.0, 0.0);

    // Below the horizon the ray would pass through the planet, where the
    // density blows up and the sky turns black. March the horizon instead and
    // turn it into ground in step 2b
    vec3 atmosDir = normalize(vec3(viewDir.x, max(viewDir.y, 0.0), viewDir.z));
    vec2 inter = raySphereIntersect(rayStart, atmosDir, atmosRadius);
    float rayLength = inter.y;

    // Phase Functions
    float mu = dot(atmosDir, sunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g = u_scatterParams.x;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

    // --- 2. Raymarching Loop ---
    vec3 sumR = vec3_splat(0.0);
    vec3 sumM = vec3_splat(0.0);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;
    float stepSize = rayLength / float(SAMPLES);
    vec3 currentPos = rayStart;
    
    for(int i = 0; i < SAMPLES; ++i) {
        vec3 p = currentPos + atmosDir * (stepSize * 0.5);
        float height = length(p) - earthRadius;
        
        // Exponential density falloff
        float hr = exp(-height / 8000.0);
        float hm = exp(-height / 1200.0);
        opticalDepthR += hr * stepSize;
        opticalDepthM += hm * stepSize;
        
        // Light path (Sun to Point P)
        vec2 sunInter = raySphereIntersect(p, sunDir, atmosRadius);
        float distToSun = sunInter.y;
        float densitySunR = exp(-height / 8000.0) * distToSun;
        float densitySunM = exp(-height / 1200.0) * distToSun;

        // Total Extinction (View Path + Sun Path)
        vec3 tau = u_betaR.rgb * (opticalDepthR + densitySunR) + u_betaM.rgb * 1.1 * (opticalDepthM + densitySunM);
        vec3 attenuation = exp(-tau);
        
        sumR += hr * attenuation;
        sumM += hm * attenuation;
        currentPos += atmosDir * stepSize;
    }

    // Calculated Sky Color
    vec3 skyColor = (sumR * u_betaR.rgb * phaseR + sumM * u_betaM.rgb * phaseM) * sunLight * stepSize;

    // --- 2b. Ground (below the horizon) ---
    // Horizon haze, fading to a darker, desaturated gray further down. Follows
    // the sky's brightness, so it dims at night with everything else
    const float groundLevel = 0.45;       // brightness vs the horizon
    const float groundSaturation = 0.35;  // how much of the sky tint it keeps
    float below = smoothstep(0.0, 0.25, -viewDir.y);
    float horizonLum = dot(skyColor, vec3(0.2126, 0.7152, 0.0722));
    vec3 groundColor = lerp(vec3_splat(horizonLum), skyColor, groundSaturation) * groundLevel;
    skyColor = lerp(skyColor, groundColor, below);
    float aboveHorizon = step(0.0, viewDir.y);

    // --- 3. Star Field Generation ---
    vec2 uv = vec2(atan2(viewDir.z, viewDir.x), asin(viewDir.y));
    vec3 stars = vec3_splat(0.0);
    float skyBrightness = dot(skyColor, vec3_splat(0.33));
    
    // Only draw stars if sky is dark
    if (skyBrightness < 0.1) {
        vec2 starUV = uv * 200.0;
        vec2 id = floor(starUV);
        float n = hash(id);
        
        if (n > 0.95) { // Threshold for star sparsity
            float size = lerp(0.05, 0.25, hash(id * 99.0));
            float d = length(frac(starUV) - 0.5);
            float star = smoothstep(size, size * 0.5, d);
            // Twinkle effect
            float twinkle = sin(u_parameters.w * 5.0 + n * 100.0) * 0.5 + 0.5;
            stars = vec3_splat(star * 10.0 * twinkle);
            stars *= smoothstep(-0.1, 0.2, viewDir.y); // Fade at horizon
        }
        
        // Milky Way Band
        vec3 galaxyNormal = normalize(vec3(0.5, 0.2, 1.0));
        float bandMask = exp(-abs(dot(viewDir, galaxyNormal)) * 3.5);
        if (bandMask > 0.01) {
             float gNoise = fbm(viewDir * 3.0);
             stars += vec3(0.1, 0.1, 0.15) * bandMask * gNoise;
        }
        stars *= (1.0 - smoothstep(0.0, 0.1, skyBrightness));
        stars *= aboveHorizon;  // nothing shows through the ground
    }

    // --- 4. Clouds (Volumetric Layer) ---
    vec3 cloudColor = vec3_splat(0.0);
    float cloudAlpha = 0.0;
    
    // Define cloud layer geometry
    float cloudLayerHeight = 3000.0;
    float cloudRadius = earthRadius + cloudLayerHeight;
    vec2 cloudInter = raySphereIntersect(rayStart, viewDir, cloudRadius);
    float distToCloud = cloudInter.y;
    
    // Only render clouds if we hit the layer and aren't looking down
    if (distToCloud > 0.0 && viewDir.y > -0.05) {
        vec3 hitPos = rayStart + viewDir * distToCloud;
        
        // Map World Position to UVs (Physics based projection)
        vec3 cloudUV = hitPos * 0.0002; 
        
        // Cloud Animation (Wind)
        cloudUV.x += u_parameters.w * 0.02;
        cloudUV.z += u_parameters.w * 0.01;
        
        float noiseVal = fbm(cloudUV);
        
        // Coverage threshold
        float coverage = 0.45; 
        float density = smoothstep(coverage, coverage + 0.4, noiseVal);
        
        if (density > 0.0) {
            float cosTheta = dot(viewDir, sunDir);
            
            // --- Cloud Color Physics ---
            
            // 1. Sun Transmittance: Calculate what color the sun is at the cloud's height
            // If sun is low, light is red. If sun is high, light is white.
            vec3 sunTransmittance = getTransmittance(sunDir.y, u_betaR.rgb, u_betaM.rgb);
            vec3 sunColorAtCloud = sunLight * sunTransmittance;

            // 2. Direct Light (Silver Lining / Phase Function)
            float sunHaze = pow(max(cosTheta, 0.0), 6.0); 
            float sunCore = pow(max(cosTheta, 0.0), 32.0); 
            vec3 sunScattering = sunColorAtCloud * (sunHaze * 2.0 + sunCore * 5.0);
            
            // 3. Ambient Light
            // Clouds reflect the ambient sky. If sky is orange, clouds are orange.
            // We boost it slightly as clouds are reflective.
            vec3 ambientTerm = skyColor * 1.5 + vec3(0.05, 0.05, 0.08); 
            
            // 4. Combine
            // Thinner clouds transmit more light, thicker clouds reflect more ambient/darkness
            float lightTransmission = lerp(1.0, 0.4, density);
            vec3 baseCloud = ambientTerm * lightTransmission + sunScattering;
            
            // Fade clouds at the far horizon to blend with sky
            float horizonFade = smoothstep(0.0, 0.1, viewDir.y);
            
            cloudColor = baseCloud;
            cloudAlpha = density * horizonFade;
        }
    }

    // --- 5. Sun Disk (Physics Based) ---
    float sunAngularSize = u_parameters.x;
    float sunEdge = smoothstep(sunAngularSize + 0.01, sunAngularSize - 0.005, acos(mu));
    
    // Calculate transmittance for the direct view path to the sun
    // Horizon -> Red, Zenith -> White
    vec3 sunDirectTransmittance = getTransmittance(viewDir.y, u_betaR.rgb, u_betaM.rgb);
    
    // Final Sun Color
    vec3 sunRender = sunLight * sunDirectTransmittance * sunEdge * aboveHorizon;
    
    // --- 6. Compositing ---
    // Start with Sky + Stars + Sun
    vec3 finalComp = skyColor + stars + sunRender;
    
    // Blend clouds on top
    // "Fog" the clouds slightly based on their alpha to blend them into the atmosphere
    vec3 cloudFogged = lerp(cloudColor, skyColor, 0.3); 
    finalComp = lerp(finalComp, cloudFogged, cloudAlpha);

    // --- 7. Post Processing ---
    finalComp = finalComp * u_parameters.z; // Exposure
    finalComp = ACESTonemap(finalComp);     // HDR -> LDR
    finalComp = toGamma(finalComp);         // Linear -> sRGB

    gl_FragColor = vec4(finalComp, 1.0);
}