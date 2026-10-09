layout(push_constant) uniform Push {
    vec4 originCell;
    float time;
    float prevTime;
    float iso;
    int grid;
    uint maxVertices;
} pc;

const int BALL_COUNT = 6;

vec3 ballCenter(int i, float t) {
    float fi = float(i);
    float a = t * (0.55 + 0.17 * fi) + fi * 1.9;
    float b = t * (0.41 + 0.11 * fi) + fi * 2.7;
    return vec3(6.25 * sin(a), 5.0 * sin(b * 1.3 + fi), 6.25 * cos(b));
}

float ballRadius(int i) {
    return 6.9 + 0.9 * float(i % 3);
}

float evalField(vec3 p, float t) {
    float sum = 0.0;
    for (int i = 0; i < BALL_COUNT; ++i) {
        float r = ballRadius(i);
        vec3 d = p - ballCenter(i, t);
        float q = 1.0 - dot(d, d) / (r * r);
        if (q > 0.0) sum += q * q * q;
    }
    return sum;
}

vec3 evalGradient(vec3 p, float t) {
    vec3 grad = vec3(0.0);
    for (int i = 0; i < BALL_COUNT; ++i) {
        float r = ballRadius(i);
        vec3 d = p - ballCenter(i, t);
        float q = 1.0 - dot(d, d) / (r * r);
        if (q > 0.0) grad += (-6.0 * q * q / (r * r)) * d;
    }
    return grad;
}
