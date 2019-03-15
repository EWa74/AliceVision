// This file is part of the AliceVision project.
// Copyright (c) 2017 AliceVision contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "DepthSimMap.hpp"
#include <aliceVision/system/Logger.hpp>
#include <aliceVision/mvsUtils/common.hpp>
#include <aliceVision/mvsUtils/fileIO.hpp>
#include <aliceVision/mvsData/Color.hpp>
#include <aliceVision/mvsData/geometry.hpp>
#include <aliceVision/mvsData/jetColorMap.hpp>
#include <aliceVision/imageIO/image.hpp>

#include <iostream>

namespace aliceVision {
namespace depthMap {

DepthSimMap::DepthSimMap(int rc, mvsUtils::MultiViewParams& mp, int scale, int step)
    : _scale( scale )
    , _step( step )
    , _mp(mp)
    , _rc(rc)
{
    _w = _mp.getWidth(_rc) / (_scale * _step);
    _h = _mp.getHeight(_rc) / (_scale * _step);
    _dsm.resize_with(_w * _h, DepthSim(-1.0f, 1.0f));
}

DepthSimMap::~DepthSimMap()
{
}

void DepthSimMap::add11(const DepthSimMap& depthSimMap)
{
    if((_scale != 1) || (_step != 1))
    {
        throw std::runtime_error("Error DepthSimMap: You can only add to scale1-step1 map.");
    }

    int k = (depthSimMap._step * depthSimMap._scale) / 2;
    int k1 = k;
    if((depthSimMap._step * depthSimMap._scale) % 2 == 0)
        k -= 1;

    for(int i = 0; i < depthSimMap._dsm.size(); i++)
    {
        int x = (i % depthSimMap._w) * depthSimMap._step * depthSimMap._scale;
        int y = (i / depthSimMap._w) * depthSimMap._step * depthSimMap._scale;
        DepthSim depthSim = depthSimMap._dsm[i];

        if(depthSim.depth > -1.0f)
        {

            bool isBest = true;
            for(int yp = y - k; yp <= y + k1; yp++)
            {
                for(int xp = x - k; xp <= x + k1; xp++)
                {
                    if((xp >= 0) && (xp < _w) && (yp >= 0) && (yp < _h) && // check image borders
                       (depthSim.sim > _dsm[yp * _w + xp].sim))
                    {
                        isBest = false;
                    }
                }
            }

            if(isBest)
            {
                for(int yp = y - k; yp <= y + k1; yp++)
                {
                    for(int xp = x - k; xp <= x + k1; xp++)
                    {
                        if((xp >= 0) && (xp < _w) && (yp >= 0) && (yp < _h))
                        {
                            _dsm[yp * _w + xp] = depthSim;
                        }
                    }
                }
            }
        }
    }
}

void DepthSimMap::add(const DepthSimMap& depthSimMap)
{
    if((_scale != depthSimMap._scale) || (_step != depthSimMap._step))
    {
        throw std::runtime_error("Error DepthSimMap: You can only add to the same _scale and step map.");
    }

    for(int i = 0; i < _dsm.size(); i++)
    {
        const DepthSim& depthSim1 = _dsm[i];
        const DepthSim& depthSim2 = depthSimMap._dsm[i];

        if((depthSim2.depth > -1.0f) && (depthSim2.sim < depthSim1.sim))
        {
            _dsm[i] = depthSim2;
        }
    }
}

Point2d DepthSimMap::getMaxMinDepth() const
{
    float maxDepth = -1.0f;
    float minDepth = std::numeric_limits<float>::max();
    for(int j = 0; j < _w * _h; j++)
    {
        if(_dsm[j].depth > -1.0f)
        {
            maxDepth = std::max(maxDepth, _dsm[j].depth);
            minDepth = std::min(minDepth, _dsm[j].depth);
        }
    }
    return Point2d(maxDepth, minDepth);
}

Point2d DepthSimMap::getMaxMinSim() const
{
    float maxSim = -1.0f;
    float minSim = std::numeric_limits<float>::max();
    for(int j = 0; j < _w * _h; j++)
    {
        if(_dsm[j].sim > -1.0f)
        {
            maxSim = std::max(maxSim, _dsm[j].sim);
            minSim = std::min(minSim, _dsm[j].sim);
        }
    }
    return Point2d(maxSim, minSim);
}

float DepthSimMap::getPercentileDepth(float perc) const
{
    int step = std::max(1, (_w * _h) / 50000);
    int n = (_w * _h) / std::max(1, (step - 1));
    StaticVector<float> depths;
    depths.reserve(n);

    for(int j = 0; j < _w * _h; j += step)
    {
        if(_dsm[j].depth > -1.0f)
        {
            depths.push_back(_dsm[j].depth);
        }
    }

    qsort(&depths[0], depths.size(), sizeof(float), qSortCompareFloatAsc);

    float out = depths[(float)((float)depths.size() * perc)];

    return out;
}

/**
 * @brief Get depth map at the size of our input image (with _scale applied)
 *        from an internal buffer only computed for a subpart (based on the step).
 */
void DepthSimMap::getDepthMapStep1(StaticVector<float>& out_depthMap) const
{
    // Size of our input image (with _scale applied)
    int wdm = _mp.getWidth(_rc) / _scale;
    int hdm = _mp.getHeight(_rc) / _scale;

    // Create a depth map at the size of our input image
    out_depthMap.resize(wdm * hdm);

    for(int i = 0; i < wdm * hdm; i++)
    {
        int x = (i % wdm) / _step;
        int y = (i / wdm) / _step;
        if((x < _w) && (y < _h))
        {
      // dsm size: (width, height) / (_scale*step)
            float depth = _dsm[y * _w + x].depth;
      // depthMap size: (width, height) / _scale
            out_depthMap[i] = depth;
        }
    }
}

void DepthSimMap::getSimMapStep1(StaticVector<float>& out_simMap) const
{
    int wdm = _mp.getWidth(_rc) / _scale;
    int hdm = _mp.getHeight(_rc) / _scale;

    out_simMap.resize(wdm * hdm);
    for(int i = 0; i < wdm * hdm; i++)
    {
        int x = (i % wdm) / _step;
        int y = (i / wdm) / _step;
        if((x < _w) && (y < _h))
        {
            float sim = _dsm[y * _w + x].sim;
            out_simMap[i] = sim;
        }
    }
}

void DepthSimMap::getDepthMapStep1XPart(StaticVector<float>& out_depthMap, int xFrom, int partW)
{
    int wdm = _mp.getWidth(_rc) / _scale;
    int hdm = _mp.getHeight(_rc) / _scale;

    out_depthMap.resize_with(wdm * hdm, -1.0f);
    for(int yp = 0; yp < hdm; yp++)
    {
        for(int xp = xFrom; xp < xFrom + partW; xp++)
        {
            int x = xp / _step;
            int y = yp / _step;
            if((x < _w) && (y < _h))
            {
                float depth = _dsm[y * _w + x].depth;
                out_depthMap[yp * partW + (xp - xFrom)] = depth;
            }
        }
    }
}

void DepthSimMap::getSimMapStep1XPart(StaticVector<float>& out_simMap, int xFrom, int partW)
{
    int wdm = _mp.getWidth(_rc) / _scale;
    int hdm = _mp.getHeight(_rc) / _scale;

    out_simMap.resize_with(wdm * hdm, -1.0f);
    for(int yp = 0; yp < hdm; yp++)
    {
        for(int xp = xFrom; xp < xFrom + partW; xp++)
        {
            int x = xp / _step;
            int y = yp / _step;
            if((x < _w) && (y < _h))
            {
                float sim = _dsm[y * _w + x].sim;
                out_simMap[yp * partW + (xp - xFrom)] = sim;
            }
        }
    }
}

void DepthSimMap::initJustFromDepthMap(const StaticVector<float>& depthMap, float defaultSim)
{
    int wdm = _mp.getWidth(_rc) / _scale;

    for(int i = 0; i < _dsm.size(); i++)
    {
        int x = (i % _w) * _step;
        int y = (i / _w) * _step;
        if((x < _w) && (y < _h))
        {
            _dsm[i].depth = depthMap[y * wdm + x];
            _dsm[i].sim = defaultSim;
        }
    }
}

void DepthSimMap::initJustFromDepthMap(const DepthSimMap& depthSimMap, float defaultSim)
{
  if(depthSimMap._w != _w || depthSimMap._h != _h)
    throw std::runtime_error("DepthSimMap:initJustFromDepthMap: Error input depth map is not at the same size.");

  for (int y = 0; y < _h; ++y)
  {
    for (int x = 0; x < _w; ++x)
    {
      DepthSim& ds = _dsm[y * _w + x];
      ds.depth = depthSimMap._dsm[y * depthSimMap._w + x].depth;
      ds.sim = defaultSim;
    }
  }
}

void DepthSimMap::initFromDepthMapAndSimMap(StaticVector<float>* depthMapT, StaticVector<float>* simMapT,
                                                 int depthSimMapsScale)
{
    int wdm = _mp.getWidth(_rc) / depthSimMapsScale;
    int hdm = _mp.getHeight(_rc) / depthSimMapsScale;

    for(int i = 0; i < _dsm.size(); i++)
    {
        int x = (((i % _w) * _step) * _scale) / depthSimMapsScale;
        int y = (((i / _w) * _step) * _scale) / depthSimMapsScale;
        if((x < wdm) && (y < hdm))
        {
            int index = y * wdm + x;
            _dsm[i].depth = (*depthMapT)[index];
            _dsm[i].sim = (*simMapT)[index];
        }
    }
}

void DepthSimMap::getDepthMap(StaticVector<float>& out_depthMap) const
{
    out_depthMap.resize(_dsm.size());
    for(int i = 0; i < _dsm.size(); i++)
    {
        out_depthMap[i] = _dsm[i].depth;
    }
}

void DepthSimMap::saveToImage(const std::string& filename, float simThr) const
{
    const int bufferWidth = 2 * _w;
    std::vector<Color> colorBuffer(bufferWidth * _h);

    try 
    {
        Point2d maxMinDepth;
        maxMinDepth.x = getPercentileDepth(0.9) * 1.1;
        maxMinDepth.y = getPercentileDepth(0.01) * 0.8;

        Point2d maxMinSim = Point2d(simThr, -1.0f);
        if(simThr < -1.0f)
        {
            Point2d autoMaxMinSim = getMaxMinSim();
            // only use it if the default range is valid
            if (std::abs(autoMaxMinSim.x - autoMaxMinSim.y) > std::numeric_limits<float>::epsilon())
                maxMinSim = autoMaxMinSim;

            if(_mp.verbose)
                ALICEVISION_LOG_DEBUG("saveToImage: max : " << maxMinSim.x << ", min: " << maxMinSim.y);
        }

        for(int y = 0; y < _h; y++)
        {
            for(int x = 0; x < _w; x++)
            {
                const DepthSim& depthSim = _dsm[y * _w + x];
                float depth = (depthSim.depth - maxMinDepth.y) / (maxMinDepth.x - maxMinDepth.y);
                colorBuffer.at(y * bufferWidth + x) = getColorFromJetColorMap(depth);

                float sim = (depthSim.sim - maxMinSim.y) / (maxMinSim.x - maxMinSim.y);
                colorBuffer.at(y * bufferWidth + _w + x) = getColorFromJetColorMap(sim);
            }
        }

        imageIO::writeImage(filename, bufferWidth, _h, colorBuffer);
    }
    catch(...)
    {
        ALICEVISION_LOG_ERROR("Failed to save '" << filename << "' (simThr: " << simThr << ")");
    }
}

void DepthSimMap::save(int rc, const StaticVector<int>& tcams) const
{
    StaticVector<float> depthMap;
    getDepthMapStep1(depthMap);
    StaticVector<float> simMap;
    getSimMapStep1(simMap);

    const int width = _mp.getWidth(rc) / _scale;
    const int height = _mp.getHeight(rc) / _scale;

    oiio::ParamValueList metadata = imageIO::getMetadataFromMap(_mp.getMetadata(rc));
    metadata.push_back(oiio::ParamValue("AliceVision:downscale", _mp.getDownscaleFactor(rc)));
    metadata.push_back(oiio::ParamValue("AliceVision:CArr", oiio::TypeDesc(oiio::TypeDesc::DOUBLE, oiio::TypeDesc::VEC3), 1, _mp.CArr[rc].m));
    metadata.push_back(oiio::ParamValue("AliceVision:iCamArr", oiio::TypeDesc(oiio::TypeDesc::DOUBLE, oiio::TypeDesc::MATRIX33), 1, _mp.iCamArr[rc].m));

    {
      const Point2d maxMinDepth = getMaxMinDepth();
      metadata.push_back(oiio::ParamValue("AliceVision:minDepth", static_cast<float>(maxMinDepth.y)));
      metadata.push_back(oiio::ParamValue("AliceVision:maxDepth", static_cast<float>(maxMinDepth.x)));
    }

    {
      std::vector<double> matrixP = _mp.getOriginalP(rc);
      metadata.push_back(oiio::ParamValue("AliceVision:P", oiio::TypeDesc(oiio::TypeDesc::DOUBLE, oiio::TypeDesc::MATRIX44), 1, matrixP.data()));
    }

    imageIO::writeImage(getFileNameFromIndex(_mp, rc, mvsUtils::EFileType::depthMap, _scale), width, height, depthMap.getDataWritable(), imageIO::EImageQuality::LOSSLESS, metadata);
    imageIO::writeImage(getFileNameFromIndex(_mp, rc, mvsUtils::EFileType::simMap, _scale), width, height, simMap.getDataWritable(), imageIO::EImageQuality::OPTIMIZED, metadata);
}

void DepthSimMap::load(int rc, int fromScale)
{
    int width, height;

    StaticVector<float> depthMap;
    StaticVector<float> simMap;

    imageIO::readImage(getFileNameFromIndex(_mp, rc, mvsUtils::EFileType::depthMap, fromScale), width, height, depthMap.getDataWritable());
    imageIO::readImage(getFileNameFromIndex(_mp, rc, mvsUtils::EFileType::simMap, fromScale), width, height, simMap.getDataWritable());

    initFromDepthMapAndSimMap(&depthMap, &simMap, fromScale);
}

void DepthSimMap::saveRefine(int rc, const std::string& depthMapFileName, const std::string& simMapFileName) const
{
    const int width = _mp.getWidth(rc);
    const int height = _mp.getHeight(rc);
    const int size = width * height;

    std::vector<float> depthMap(size);
    std::vector<float> simMap(size);

    for(int i = 0; i < _dsm.size(); ++i)
    {
        depthMap.at(i) = _dsm[i].depth;
        simMap.at(i) = _dsm[i].sim;
    }

    oiio::ParamValueList metadata = imageIO::getMetadataFromMap(_mp.getMetadata(rc));
    metadata.push_back(oiio::ParamValue("AliceVision:downscale", _mp.getDownscaleFactor(rc)));
    metadata.push_back(oiio::ParamValue("AliceVision:CArr", oiio::TypeDesc(oiio::TypeDesc::DOUBLE, oiio::TypeDesc::VEC3), 1, _mp.CArr[rc].m));
    metadata.push_back(oiio::ParamValue("AliceVision:iCamArr", oiio::TypeDesc(oiio::TypeDesc::DOUBLE, oiio::TypeDesc::MATRIX33), 1, _mp.iCamArr[rc].m));

    {
      const Point2d maxMinDepth = getMaxMinDepth();
      metadata.push_back(oiio::ParamValue("AliceVision:minDepth", static_cast<float>(maxMinDepth.y)));
      metadata.push_back(oiio::ParamValue("AliceVision:maxDepth", static_cast<float>(maxMinDepth.x)));
    }

    {
        std::vector<double> matrixP = _mp.getOriginalP(rc);
        metadata.push_back(oiio::ParamValue("AliceVision:P", oiio::TypeDesc(oiio::TypeDesc::DOUBLE, oiio::TypeDesc::MATRIX44), 1, matrixP.data()));
    }

    imageIO::writeImage(depthMapFileName, width, height, depthMap, imageIO::EImageQuality::LOSSLESS, metadata);
    imageIO::writeImage(simMapFileName, width, height, simMap, imageIO::EImageQuality::OPTIMIZED, metadata);
}

float DepthSimMap::getCellSmoothStep(int rc, const int cellId)
{
    return getCellSmoothStep(rc, Pixel(cellId % _w, cellId / _w));
}

float DepthSimMap::getCellSmoothStep(int rc, const Pixel& cell)
{
    if((cell.x <= 0) || (cell.x >= _w - 1) || (cell.y <= 0) || (cell.y >= _h - 1))
    {
        return 0.0f;
    }

    Pixel cell0 = cell;
    Pixel cellL = cell0 + Pixel(0, -1);
    Pixel cellR = cell0 + Pixel(0, 1);
    Pixel cellU = cell0 + Pixel(-1, 0);
    Pixel cellB = cell0 + Pixel(1, 0);

    float d0 = _dsm[cell0.y * _w + cell0.x].depth;
    float dL = _dsm[cellL.y * _w + cellL.x].depth;
    float dR = _dsm[cellR.y * _w + cellR.x].depth;
    float dU = _dsm[cellU.y * _w + cellU.x].depth;
    float dB = _dsm[cellB.y * _w + cellB.x].depth;

    Point3d cg = Point3d(0.0f, 0.0f, 0.0f);
    float n = 0.0f;

    if(dL > 0.0f)
    {
        cg =
            cg +
            (_mp.CArr[rc] +
             (_mp.iCamArr[rc] * Point2d((float)cellL.x * (_scale * _step), (float)cellL.y * (_scale * _step))).normalize() *
                 dL);
        n += 1.0f;
    }
    if(dR > 0.0f)
    {
        cg =
            cg +
            (_mp.CArr[rc] +
             (_mp.iCamArr[rc] * Point2d((float)cellR.x * (_scale * _step), (float)cellR.y * (_scale * _step))).normalize() *
                 dR);
        n += 1.0f;
    }
    if(dU > 0.0f)
    {
        cg =
            cg +
            (_mp.CArr[rc] +
             (_mp.iCamArr[rc] * Point2d((float)cellU.x * (_scale * _step), (float)cellU.y * (_scale * _step))).normalize() *
                 dU);
        n += 1.0f;
    }
    if(dB > 0.0f)
    {
        cg =
            cg +
            (_mp.CArr[rc] +
             (_mp.iCamArr[rc] * Point2d((float)cellB.x * (_scale * _step), (float)cellB.y * (_scale * _step))).normalize() *
                 dB);
        n += 1.0f;
    }

    if((d0 > 0.0f) && (n > 1.0f))
    {
        cg = cg / n;
        Point3d p0 =
            _mp.CArr[rc] +
            (_mp.iCamArr[rc] * Point2d((float)cell0.x * (_scale * _step), (float)cell0.y * (_scale * _step))).normalize() *
                d0;
        Point3d vcn = (_mp.CArr[rc] - p0).normalize();

        Point3d pS = closestPointToLine3D(&cg, &p0, &vcn);

        return (_mp.CArr[rc] - pS).size() - d0;
    }

    return 0.0f;
}

} // namespace depthMap
} // namespace aliceVision
