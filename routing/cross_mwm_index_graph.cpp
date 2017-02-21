#include "routing/cross_mwm_index_graph.hpp"
#include "routing/cross_mwm_road_graph.hpp"
#include "routing/osrm_path_segment_factory.hpp"

#include "base/macros.hpp"
#include "base/stl_helpers.hpp"

#include <algorithm>
#include <utility>

using namespace platform;
using namespace routing;
using namespace std;

namespace
{
/// \returns FtSeg by |segment|.
OsrmMappingTypes::FtSeg GetFtSeg(Segment const & segment)
{
  uint32_t const startPnt = segment.IsForward() ? segment.GetPointId(false /* front */)
                                                : segment.GetPointId(true /* front */);
  uint32_t const endPnt = segment.IsForward() ? segment.GetPointId(true /* front */)
                                              : segment.GetPointId(false /* front */);
  return OsrmMappingTypes::FtSeg(segment.GetFeatureId(), startPnt, endPnt);
}

/// \returns a pair of direct and reverse(backward) node id by |segment|.
pair<TOsrmNodeId, TOsrmNodeId> GetDirectAndReverseNodeId(
    OsrmFtSegMapping const & segMapping, Segment const & segment)
{
  OsrmFtSegMapping::TFtSegVec const ftSegs = {GetFtSeg(segment)};
  OsrmFtSegMapping::OsrmNodesT nodeIds;
  segMapping.GetOsrmNodes(ftSegs, nodeIds);
  CHECK_LESS(nodeIds.size(), 2, ());
  return nodeIds.empty() ? make_pair(INVALID_NODE_ID, INVALID_NODE_ID) : nodeIds.begin()->second;
}

/// \returns a node id by segment according to segment direction. If there's no
/// a valid node is for |segment| returns INVALID_NODE_ID.
TOsrmNodeId GetNodeId(OsrmFtSegMapping const & segMapping, Segment const & segment)
{
  auto const directAndReverseNodeId = GetDirectAndReverseNodeId(segMapping, segment);
  return segment.IsForward() ? directAndReverseNodeId.first : directAndReverseNodeId.second;
}

bool GetFirstValidSegment(OsrmFtSegMapping const & segMapping, NumMwmId numMwmId,
                          TWrittenNodeId nodeId, Segment & segment)
{
  auto const range = segMapping.GetSegmentsRange(nodeId);
  for (size_t segmentIndex = range.first; segmentIndex != range.second; ++segmentIndex)
  {
    OsrmMappingTypes::FtSeg seg;
    // The meaning of node id in osrm is an edge between two joints.
    // So, it's possible to consider the first valid segment from the range which returns by GetSegmentsRange().
    segMapping.GetSegmentByIndex(segmentIndex, seg);

    if (!seg.IsValid())
      continue;

    CHECK_NOT_EQUAL(seg.m_pointStart, seg.m_pointEnd, ());
    segment = Segment(numMwmId, seg.m_fid, min(seg.m_pointStart, seg.m_pointEnd), seg.IsForward());
    TOsrmNodeId n = GetNodeId(segMapping, segment);
    if (n != nodeId)
      CHECK(false, ());
    return true;
  }
  LOG(LERROR, ("No valid segments in the range returned by OsrmFtSegMapping::GetSegmentsRange(", nodeId,
               "). Num mwm id:", numMwmId));
  return false;
}

//bool GetFirstValidSegmentBySegment(OsrmFtSegMapping const & segMapping, Segment const & segment,
//                                   Segment & firstValidSegment)
//{
//  TOsrmNodeId const nodeId = GetNodeId(segMapping, segment);
//  if (nodeId == INVALID_NODE_ID)
//    return false;

//  return GetFirstValidSegment(segMapping, segment.GetMwmId(), nodeId, firstValidSegment);
//}

void AddFirstValidSegment(OsrmFtSegMapping const & segMapping, NumMwmId numMwmId,
                          TWrittenNodeId nodeId, vector<Segment> & segments)
{
  Segment key;
  if (GetFirstValidSegment(segMapping, numMwmId, nodeId, key))
    segments.push_back(key);
}

void FillTransitionSegments(OsrmFtSegMapping const & segMapping, TWrittenNodeId nodeId,
                            NumMwmId numMwmId, ms::LatLon const & latLon,
                            std::map<Segment, std::vector<ms::LatLon>> & transitionSegments)
{
  Segment key;
  if (!GetFirstValidSegment(segMapping, numMwmId, nodeId, key))
    return;

  transitionSegments[key].push_back(latLon);
}

vector<ms::LatLon> const & GetLatLon(std::map<Segment, std::vector<ms::LatLon>> const & segMap,
                                     Segment const & s)
{
  auto it = segMap.find(s);
  CHECK(it != segMap.cend(), ());
  return it->second;
}

void AddSegmentEdge(NumMwmIds const & numMwmIds, OsrmFtSegMapping const & segMapping,
                    CrossWeightedEdge const & osrmEdge, bool isOutgoing, NumMwmId numMwmId,
                    vector<SegmentEdge> & edges)
{
  BorderCross const & target = osrmEdge.GetTarget();
  CrossNode const & crossNode = isOutgoing ? target.fromNode : target.toNode;

  if (!crossNode.mwmId.IsAlive())
    return;

  NumMwmId const crossNodeMwmId = numMwmIds.GetId(crossNode.mwmId.GetInfo()->GetLocalFile().GetCountryFile());
  CHECK_EQUAL(numMwmId, crossNodeMwmId, ());

  Segment segment;
  if (!GetFirstValidSegment(segMapping, crossNodeMwmId, crossNode.node, segment))
    return;

  edges.emplace_back(segment, osrmEdge.GetWeight() * kOSRMWeightToSecondsMultiplier);
}
}  // namespace

namespace routing
{
CrossMwmIndexGraph::CrossMwmIndexGraph(shared_ptr<NumMwmIds> numMwmIds, RoutingIndexManager & indexManager)
  : m_indexManager(indexManager), m_numMwmIds(numMwmIds)
{
  InitCrossMwmGraph();
}

CrossMwmIndexGraph::~CrossMwmIndexGraph() {}

bool CrossMwmIndexGraph::IsTransition(Segment const & s, bool isOutgoing)
{
  // @TODO(bykoianko) It's necessary to check if mwm of |s| contains an A* cross mwm section
  // and if so to use it. If not, osrm cross mwm sections should be used.

  // Checking if a segment |s| is a transition segment by osrm cross mwm sections.
  TransitionSegments const & t = GetSegmentMaps(s.GetMwmId());

  if (isOutgoing)
    return t.m_outgoing.count(s) != 0;
  return t.m_ingoing.count(s) != 0;
}

void CrossMwmIndexGraph::GetTwins(Segment const & s, bool isOutgoing, vector<Segment> & twins)
{
  CHECK(IsTransition(s, isOutgoing), ("The segment is not a transition segment."));
  twins.clear();
  // @TODO(bykoianko) It's necessary to check if mwm of |s| contains an A* cross mwm section
  // and if so to use it. If not, osrm cross mwm sections should be used.

  auto const getTwins = [&](NumMwmId /* numMwmId */, TRoutingMappingPtr const & segMapping)
  {
    vector<string> const & neighboringMwm = segMapping->m_crossContext.GetNeighboringMwmList();

    for (string const & name : neighboringMwm)
      InsertWholeMwmTransitionSegments(m_numMwmIds->GetId(CountryFile(name)));

    auto it = m_transitionCache.find(s.GetMwmId());
    CHECK(it != m_transitionCache.cend(), ());

    vector<ms::LatLon> const & latLons = isOutgoing ? GetLatLon(it->second.m_outgoing, s)
                                                    : GetLatLon(it->second.m_ingoing, s);
    for (string const & name : neighboringMwm)
    {
      auto const addFirstValidSegment = [&](NumMwmId numMwmId, TRoutingMappingPtr const & mapping)
      {
        for (ms::LatLon const & ll : latLons)
        {
          if (isOutgoing)
          {
            mapping->m_crossContext.ForEachIngoingNodeNearPoint(ll, [&](IngoingCrossNode const & node){
              AddFirstValidSegment(mapping->m_segMapping, numMwmId, node.m_nodeId, twins);
            });
          }
          else
          {
            mapping->m_crossContext.ForEachOutgoingNodeNearPoint(ll, [&](OutgoingCrossNode const & node){
              AddFirstValidSegment(mapping->m_segMapping, numMwmId, node.m_nodeId, twins);
            });
          }
        }
      };

      if (!LoadWith(m_numMwmIds->GetId(CountryFile(name)), addFirstValidSegment))
        continue;  // mwm was not loaded.
    }
  };

  LoadWith(s.GetMwmId(), getTwins);
  my::SortUnique(twins);

//  for (Segment const & twin : twins)
//  {
//    std::map<Segment, ms::LatLon> const & ingoingSeg = GetSegmentMaps(twin.GetMwmId()).m_ingoing;
//    std::map<Segment, ms::LatLon> const & outgoingSeg = GetSegmentMaps(twin.GetMwmId()).m_outgoing;
//    bool ingoing = false;
//    bool outgoing = false;

//    if (ingoingSeg.find(twin) != ingoingSeg.cend())
//      ingoing = true;
//    if (outgoingSeg.find(twin) != outgoingSeg.cend())
//      outgoing = true;

//    CHECK(ingoing || outgoing, ());
//  }
}

void CrossMwmIndexGraph::GetEdgeList(Segment const & s,
                                     bool isOutgoing, vector<SegmentEdge> & edges)
{
  // @TODO(bykoianko) It's necessary to check if mwm of |s| contains an A* cross mwm section
  // and if so to use it. If not, osrm cross mwm sections should be used.
  if (!isOutgoing)
    return;

  edges.clear();
  auto const  fillEdgeList = [&](NumMwmId /* numMwmId */, TRoutingMappingPtr const & mapping){
    vector<BorderCross> borderCrosses;
    GetBorderCross(s, isOutgoing, borderCrosses);

    for (BorderCross const v : borderCrosses)
    {
      vector<CrossWeightedEdge> adj;
      if (isOutgoing)
        m_crossMwmGraph->GetOutgoingEdgesList(v, adj);
      else
        m_crossMwmGraph->GetOutgoingEdgesList(v, adj);

      for (CrossWeightedEdge const & a : adj)
        AddSegmentEdge(*m_numMwmIds, mapping->m_segMapping, a, isOutgoing, s.GetMwmId(), edges);
    }
  };
  LoadWith(s.GetMwmId(), fillEdgeList);
}

void CrossMwmIndexGraph::Clear()
{
  InitCrossMwmGraph();
  m_transitionCache.clear();
}

void CrossMwmIndexGraph::InitCrossMwmGraph()
{
  m_crossMwmGraph = make_unique<CrossMwmGraph>(m_indexManager);
}

void CrossMwmIndexGraph::InsertWholeMwmTransitionSegments(NumMwmId numMwmId)
{
  if (m_transitionCache.count(numMwmId) != 0)
    return;

  auto const fillAllTransitionSegments = [this](NumMwmId numMwmId, TRoutingMappingPtr const & mapping){
    TransitionSegments transitionSegments;
    mapping->m_crossContext.ForEachOutgoingNode([&](OutgoingCrossNode const & node)
    {
      FillTransitionSegments(mapping->m_segMapping, node.m_nodeId, numMwmId,
                             node.m_point, transitionSegments.m_outgoing);
    });
    mapping->m_crossContext.ForEachIngoingNode([&](IngoingCrossNode const & node)
    {
      FillTransitionSegments(mapping->m_segMapping, node.m_nodeId, numMwmId,
                             node.m_point, transitionSegments.m_ingoing);
    });
    auto const p = m_transitionCache.emplace(numMwmId, move(transitionSegments));
    UNUSED_VALUE(p);
    ASSERT(p.second, ("Mwm num id:", numMwmId, "has been inserted before. Country file name:",
                      mapping->GetCountryName()));
  };

  if (!LoadWith(numMwmId, fillAllTransitionSegments))
    m_transitionCache.emplace(numMwmId, TransitionSegments());
}

bool CrossMwmIndexGraph::GetBorderCross(Segment const & s, bool isOutgoing,
                                        vector<BorderCross> & borderCrosses)
{
  if (!isOutgoing)
    NOTIMPLEMENTED();

  auto const fillBorderCrosses = [&](NumMwmId /* inNumMwmId */, TRoutingMappingPtr const & inMapping){
    // ingoing edge
    pair<TOsrmNodeId, TOsrmNodeId> const directReverseTo =
        GetDirectAndReverseNodeId(inMapping->m_segMapping, s);
    vector<ms::LatLon> const & ingoingTransitionPnt = GetIngoingTransitionPnt(s);
    CHECK(!ingoingTransitionPnt.empty(), ());

    vector<CrossNode> toNodes;
    for (ms::LatLon const & p : ingoingTransitionPnt)
      toNodes.emplace_back(directReverseTo.first, directReverseTo.second, inMapping->GetMwmId(), p);

    // outgoing edge
    vector<Segment> twins;
    GetTwins(s, !isOutgoing, twins);
    for (Segment const & twin : twins)
    {
      auto const fillBorderCrossOut = [&](NumMwmId /* outNumMwmId */, TRoutingMappingPtr const & outMapping){

        pair<TOsrmNodeId, TOsrmNodeId> directReverseFrom =
            GetDirectAndReverseNodeId(outMapping->m_segMapping, twin);

        vector<ms::LatLon> const & outgoingTransitionPnt = GetOutgoingTransitionPnt(twin);
        CHECK(!outgoingTransitionPnt.empty(), ());
        for (CrossNode const & toNode : toNodes)
        {
          BorderCross bc;
          bc.toNode = toNode;
          for (ms::LatLon const & ll : outgoingTransitionPnt)
          {
            bc.fromNode = CrossNode(directReverseFrom.first, directReverseFrom.second,
                                    outMapping->GetMwmId(), ll);
            borderCrosses.push_back(move(bc));
          }
        }
      };
      LoadWith(twin.GetMwmId(), fillBorderCrossOut);
    }
  };

  if (!LoadWith(s.GetMwmId(), fillBorderCrosses))
    return false;
  return true;
}

CrossMwmIndexGraph::TransitionSegments const & CrossMwmIndexGraph::GetSegmentMaps(NumMwmId numMwmId)
{
  auto it = m_transitionCache.find(numMwmId);
  if (it == m_transitionCache.cend())
  {
    InsertWholeMwmTransitionSegments(numMwmId);
    it = m_transitionCache.find(numMwmId);
  }
  CHECK(it != m_transitionCache.cend(), ("Mwm ", numMwmId, "has not been downloaded."));
  return it->second;
}

vector<ms::LatLon> const & CrossMwmIndexGraph::GetIngoingTransitionPnt(Segment const & s)
{
  auto const & ingoingSeg = GetSegmentMaps(s.GetMwmId()).m_ingoing;
  auto const it = ingoingSeg.find(s);
  CHECK(it != ingoingSeg.cend(), ());
  return it->second;
}

vector<ms::LatLon> const & CrossMwmIndexGraph::GetOutgoingTransitionPnt(Segment const & s)
{
  auto const & outgoingSeg = GetSegmentMaps(s.GetMwmId()).m_outgoing;
  auto const it = outgoingSeg.find(s);
  CHECK(it != outgoingSeg.cend(), ());
  return it->second;
}
}  // namespace routing
