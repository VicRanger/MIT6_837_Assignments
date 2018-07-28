//
// Created by Nycshisan on 2018/7/23.
//

#include "raytracer.h"

#include <vector>

#include "rayTree.h"

static float _err = 1e-3f;

RayTracer::RayTracer(const std::shared_ptr<SceneParser> &scParser, const shared_ptr<CommandLineArgumentParser> &cmdlParser) {
    _sceneParser = scParser;
    _cmdlParser = cmdlParser;

    _bgColor = _sceneParser->getBackgroundColor();
    _group = _sceneParser->getGroup();
    _nLights = static_cast<unsigned long>(_sceneParser->getNumLights());
    _lights = std::vector<Light*>(_nLights);
    for (int i = 0; i < _nLights; ++i) {
        _lights[i] = _sceneParser->getLight(i);
    }
    _ambient = _sceneParser->getAmbientLight();
}

Vec3f RayTracer::traceRay(Ray &ray, float tmin, int bounces, float weight, float indexOfRefraction, Hit &hit) const {
    // stop criteria - recursion depth
    if (bounces > _cmdlParser->max_bounces)
        return Vec3f(0.f, 0.f, 0.f);
    // stop criteria - small contribution
    if (weight < _cmdlParser->cutoff_weight)
        return Vec3f(0.f, 0.f, 0.f);

    Vec3f color;
    if (_group->intersect(ray, hit, tmin)) {
        color = Vec3f(0.f, 0.f, 0.f);

        auto norm = hit.getNormal();
        bool isBackSide = norm.Dot3(ray.getDirection()) > 0;
        if (_cmdlParser->shade_back && isBackSide) {
            norm.Negate();
        }

        auto m = hit.getMaterial();

        // add help line
        if (bounces == 0)
            RayTree::SetMainSegment(ray, 0, hit.getT());

        color = _ambient * m->getDiffuseColor();
        auto intersection = hit.getIntersectionPoint();
        for (int i = 0; i < _nLights; ++i) {
            Vec3f dirToLight, lightColor;
            float distToLight;
            _lights[i]->getIllumination(intersection, dirToLight, lightColor, distToLight);

            // handle shadows
            if (_cmdlParser->cast_shadow) {
                Ray shadowRay(intersection, dirToLight);
                Hit shadowHit;
                if (_group->intersect(shadowRay, shadowHit, _err) && shadowHit.getT() < distToLight) {
                    RayTree::AddShadowSegment(shadowRay, 0, shadowHit.getT());
                    continue;
                } else {
                    RayTree::AddShadowSegment(shadowRay, 0, distToLight);
                };
            }

            color += m->Shade(ray, hit, dirToLight, lightColor);
        }


        // handle reflection
        auto reflectColor = m->getReflectiveColor();
        if (reflectColor.Length() > _err) {
            Hit reflectHit;
            Ray reflectRay(intersection, _mirrorDirection(norm, ray.getDirection()));
            color += reflectColor * traceRay(reflectRay, _err, bounces + 1, weight * reflectColor.Length(),
                                             indexOfRefraction, reflectHit);
            if (bounces < _cmdlParser->max_bounces)
                RayTree::AddReflectedSegment(reflectRay, 0, reflectHit.getT());
        }

        // handle refraction
        auto transparentColor = m->getTransparentColor();
        auto materialIndexOfRefraction = isBackSide ? 1.f : m->getIndexOfRefraction(); // handle ray that goes out of the object
        if (transparentColor.Length() > _err) {
            Vec3f transmittedDir;
            // use back-shaded normal vector
            auto transmitted = _transmittedDirection(norm, ray.getDirection(), indexOfRefraction,
                                                     materialIndexOfRefraction, transmittedDir);
            if (transmitted) {
                Hit refractHit;
                Ray refractRay(intersection, transmittedDir);
                color += transparentColor *
                         traceRay(refractRay, _err, bounces + 1, weight * transparentColor.Length(),
                                  materialIndexOfRefraction, refractHit);
                if (bounces < _cmdlParser->max_bounces)
                    RayTree::AddTransmittedSegment(refractRay, 0, refractHit.getT());
            }
        }
    } else {
        color = _bgColor;
    }

    return color;
}

Vec3f RayTracer::_mirrorDirection(const Vec3f &normal, const Vec3f &incoming) const {
    // R = V – 2 (V · N) N
    auto reflectDir = incoming - 2 * (incoming.Dot3(normal)) * normal;
    reflectDir.Normalize();
    return reflectDir;
}

bool RayTracer::_transmittedDirection(const Vec3f &normal, const Vec3f &incoming, float index_i, float index_t,
                                      Vec3f &transmittedDir) const {
    // [ηr * (N · I) – √(1 – ηr^2 * (1 – (N · I)^2))] * N – ηr * I
    auto i = incoming;
    i.Negate();
    auto eta = index_i / index_t;
    auto cosThetaTSquare = 1.f - eta * eta * (1.f - normal.Dot3(i) * normal.Dot3(i));
    if (cosThetaTSquare < 0.f)
        return false;
    transmittedDir = (eta * (normal.Dot3(i)) - sqrtf(cosThetaTSquare)) * normal - eta * i;
    transmittedDir.Normalize();
    return true;
}
