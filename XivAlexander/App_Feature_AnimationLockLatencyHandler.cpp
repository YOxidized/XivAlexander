﻿#include "pch.h"
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

class App::Feature::AnimationLockLatencyHandler::Internals {
public:
	// Server responses have been usually taking between 50ms and 100ms on below-1ms
	// latency to server, so 75ms is a good average.
	// The server will do sanity check on the frequency of action use requests,
	// and it's very easy to identify whether you're trying to go below allowed minimum value.
	// This addon is already in gray area. Do NOT decrease this value. You've been warned.
	// Feel free to increase and see how does it feel like to play on high latency instead, though.
	static inline const int64_t ExtraDelay = 75;  // in milliseconds

	// On unstable network connection, limit the possible overshoot in ExtraDelay.
	static inline const int64_t MaximumExtraDelay = 150;  // in milliseconds

	static inline const int64_t AutoAttackDelay = 100; // in milliseconds

	class SingleConnectionHandler {

		struct PendingAction {
			uint32_t ActionId;
			uint32_t Sequence;
			uint64_t RequestTimestamp;
			uint64_t ResponseTimestamp = 0;
			bool CastFlag = false;
			int64_t OriginalWaitTime = 0;

			PendingAction()
				: ActionId(0)
				, Sequence(0)
				, RequestTimestamp(0) {
			}

			explicit PendingAction(const Network::Structures::IPCMessageDataType::C2S_ActionRequest& request)
				: ActionId(request.ActionId)
				, Sequence(request.Sequence)
				, RequestTimestamp(Utils::GetHighPerformanceCounter()) {
			}
		};

	public:
		// The game will allow the user to use an action, if server does not respond in 500ms since last action usage.
		// This will result in cancellation of following actions, so to prevent this, we keep track of outgoing action
		// request timestamps, and stack up required animation lock time responses from server.
		// The game will only process the latest animation lock duration information.
		std::deque<PendingAction> m_pendingActions;
		PendingAction m_latestSuccessfulRequest;
		uint64_t m_lastAnimationLockEndsAt = 0;
		std::map<int, uint64_t> m_originalWaitTimeMap;

		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace Network::Structures;

			const auto& gameConfig = Config::Instance().Game;
			const auto& runtimeConfig = Config::Instance().Runtime;

			conn.AddOutgoingFFXIVMessageHandler(this, [&](FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->Data.IPC.SubType == gameConfig.C2S_ActionRequest[0]
						|| pMessage->Data.IPC.SubType == gameConfig.C2S_ActionRequest[1]) {
						const auto& actionRequest = pMessage->Data.IPC.Data.C2S_ActionRequest;
						m_pendingActions.emplace_back(actionRequest);

						// If somehow latest action request has been made before last animation lock end time, keep it.
						// Otherwise...
						if (m_pendingActions.back().RequestTimestamp > m_lastAnimationLockEndsAt) {

							// If there was no action queued to begin with before the current one, update the base lock time to now.
							if (m_pendingActions.size() == 1)
								m_lastAnimationLockEndsAt = m_pendingActions.back().RequestTimestamp;
						}

						if (runtimeConfig.UseHighLatencyMitigationLogging)
							Misc::Logger::GetLogger().Format(
								LogCategory::AnimationLockLatencyHandler,
								"%zx: C2S_ActionRequest(%04x): actionId=%04x sequence=%04x",
								conn.GetSocket(),
								pMessage->Data.IPC.SubType,
								actionRequest.ActionId,
								actionRequest.Sequence);
					}
				}
				return true;
				});
			conn.AddIncomingFFXIVMessageHandler(this, [&](FFXIVMessage* pMessage, std::vector<uint8_t>& additionalMessages) {
				const auto now = Utils::GetHighPerformanceCounter();

				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::CustomType) {
					if (pMessage->Data.IPC.SubType == static_cast<uint16_t>(IpcCustomSubtype::OriginalWaitTime)) {
						const auto& data = pMessage->Data.IPC.Data.S2C_Custom_OriginalWaitTime;
						m_originalWaitTimeMap[data.SourceSequence] = static_cast<uint64_t>(static_cast<double>(data.OriginalWaitTime) * 1000ULL);
					}

					// Don't relay custom IPC data to game.
					return false;

				} else if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					// Only interested in messages intended for the current player
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (gameConfig.S2C_ActionEffects[0] == pMessage->Data.IPC.SubType
							|| gameConfig.S2C_ActionEffects[1] == pMessage->Data.IPC.SubType
							|| gameConfig.S2C_ActionEffects[2] == pMessage->Data.IPC.SubType
							|| gameConfig.S2C_ActionEffects[3] == pMessage->Data.IPC.SubType
							|| gameConfig.S2C_ActionEffects[4] == pMessage->Data.IPC.SubType) {

							// actionEffect has to be modified later on, so no const
							auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;
							int64_t originalWaitTime, waitTime;

							std::string extraMessage;

							{
								const auto it = m_originalWaitTimeMap.find(actionEffect.SourceSequence);
								if (it == m_originalWaitTimeMap.end())
									waitTime = originalWaitTime = static_cast<int64_t>(static_cast<double>(actionEffect.AnimationLockDuration) * 1000ULL);
								else {
									waitTime = originalWaitTime = it->second;
									m_originalWaitTimeMap.erase(it);
								}
							}

							if (actionEffect.SourceSequence == 0) {
								// Process actions originating from server.
								if (!m_latestSuccessfulRequest.CastFlag && m_latestSuccessfulRequest.Sequence && m_lastAnimationLockEndsAt > now) {
									m_latestSuccessfulRequest.ActionId = actionEffect.ActionId;
									m_latestSuccessfulRequest.Sequence = 0;
									m_lastAnimationLockEndsAt += (originalWaitTime + now) - (m_latestSuccessfulRequest.OriginalWaitTime + m_latestSuccessfulRequest.ResponseTimestamp);
									m_lastAnimationLockEndsAt = std::max(m_lastAnimationLockEndsAt, now + AutoAttackDelay);
									waitTime = m_lastAnimationLockEndsAt - now;
								}

							} else {
								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty() && m_pendingActions.front().Sequence != actionEffect.SourceSequence) {
									const auto& item = m_pendingActions.front();
									Misc::Logger::GetLogger().Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId=%04x sequence=%04x",
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty()) {
									m_latestSuccessfulRequest = m_pendingActions.front();

									// 100ms animation lock after cast ends stays. Modify animation lock duration for instant actions only.
									// Since no other action is in progress right before the cast ends, we can safely replace the animation lock with the latest after-cast lock.
									if (!m_latestSuccessfulRequest.CastFlag) {
										const auto rtt = static_cast<int64_t>(now - m_latestSuccessfulRequest.RequestTimestamp);
										conn.ApplicationLatency.AddValue(rtt);

										extraMessage = Utils::FormatString(" rtt=%lldms", rtt);

										m_latestSuccessfulRequest.ResponseTimestamp = now;

										auto delay = ExtraDelay;

										if (int64_t latency; conn.GetCurrentNetworkLatency(latency) && runtimeConfig.UseAutoAdjustingExtraDelay) {
											delay = rtt;

											auto latencyAdjusted = latency;

											extraMessage += Utils::FormatString(" latency=%lldms", latencyAdjusted);

											// Update latency statistics
											conn.NetworkLatency.AddValue(latencyAdjusted);

											if (runtimeConfig.UseLatencyCorrection) {
												// Get server response time statistic data.
												const auto rttMin = conn.ApplicationLatency.Min();
												const auto rttMean = conn.ApplicationLatency.Mean();
												const auto rttDeviation = conn.ApplicationLatency.Deviation();

												// Get latency statistic data.
												const auto latencyMean = conn.NetworkLatency.Mean();
												const auto latencyDeviation = conn.NetworkLatency.Deviation();

												// Correct latency and server response time values in case of outliers.
												latencyAdjusted = std::min(std::max(latencyMean - latencyDeviation, latencyAdjusted), latencyMean + latencyDeviation);
												delay = std::min(std::max(rttMean - rttDeviation, delay), rttMean + rttDeviation);

												// Estimate latency based on server response time statistics.
												const auto latencyEstimate = (delay + rttMin + rttMean) / 3 - rttDeviation;

												extraMessage += Utils::FormatString(" (%lldms)", latencyEstimate);

												// Correct latency value based on estimate if server response time is stable.
												latencyAdjusted = std::max(latencyEstimate, latencyAdjusted);
											}

											// This delay is based on server's processing time. If the server is busy, everyone should feel the same effect.
											// Only the player's ping is taken out of the equation.
											delay = std::max(0LL, (delay % originalWaitTime) - latencyAdjusted);

											// Prevent accidentally too high ExtraDelay.
											delay = std::min(MaximumExtraDelay, delay);
											
											extraMessage += Utils::FormatString(" delay=%lldms", delay);
										}

										m_latestSuccessfulRequest.OriginalWaitTime = originalWaitTime;
										m_lastAnimationLockEndsAt += originalWaitTime + delay;
										waitTime = m_lastAnimationLockEndsAt - now;
									}
									m_pendingActions.pop_front();
								}
							}
							if (waitTime != originalWaitTime) {
								actionEffect.AnimationLockDuration = std::max(0LL, waitTime) / 1000.f;

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Misc::Logger::GetLogger().Format(
										LogCategory::AnimationLockLatencyHandler,
										"%zx: S2C_ActionEffect(%04x): actionId=%04x sourceSequence=%04x wait=%lldms->%lldms%s",
										conn.GetSocket(),
										pMessage->Data.IPC.SubType,
										actionEffect.ActionId,
										actionEffect.SourceSequence,
										originalWaitTime, waitTime,
										extraMessage.c_str());

							} else {
								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Misc::Logger::GetLogger().Format(
										LogCategory::AnimationLockLatencyHandler,
										"%zx: S2C_ActionEffect(%04x): actionId=%04x sourceSequence=%04x wait=%llums",
										conn.GetSocket(),
										pMessage->Data.IPC.SubType,
										actionEffect.ActionId,
										actionEffect.SourceSequence,
										originalWaitTime);
							}

						} else if (pMessage->Data.IPC.SubType == gameConfig.S2C_ActorControlSelf) {
							auto& actorControlSelf = pMessage->Data.IPC.Data.S2C_ActorControlSelf;

							// Oldest action request has been rejected from server.
							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								const auto& rollback = actorControlSelf.Rollback;

								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty()
									&& (
										// Sometimes SourceSequence is empty, in which case, we use ActionId to judge.
										(rollback.SourceSequence != 0 && m_pendingActions.front().Sequence != rollback.SourceSequence)
										|| (rollback.SourceSequence == 0 && m_pendingActions.front().ActionId != rollback.ActionId)
										)) {
									const auto& item = m_pendingActions.front();
									Misc::Logger::GetLogger().Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId=%04x sequence=%04x",
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Misc::Logger::GetLogger().Format(
										LogCategory::AnimationLockLatencyHandler,
										"%zx: S2C_ActorControlSelf/ActionRejected: actionId=%04x sourceSequence=%08x",
										conn.GetSocket(),
										rollback.ActionId,
										rollback.SourceSequence);
							}

						} else if (pMessage->Data.IPC.SubType == gameConfig.S2C_ActorControl) {
							const auto& actorControl = pMessage->Data.IPC.Data.S2C_ActorControl;

							// The server has cancelled an oldest action (which is a cast) in progress.
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;

								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty() && m_pendingActions.front().ActionId != cancelCast.ActionId) {
									const auto& item = m_pendingActions.front();
									Misc::Logger::GetLogger().Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId=%04x sequence=%04x",
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Misc::Logger::GetLogger().Format(
										LogCategory::AnimationLockLatencyHandler,
										"%zx: S2C_ActorControl/CancelCast: actionId=%04x",
										conn.GetSocket(),
										cancelCast.ActionId);
							}

						} else if (pMessage->Data.IPC.SubType == gameConfig.S2C_ActorCast) {
							const auto& actorCast = pMessage->Data.IPC.Data.S2C_ActorCast;
							// Mark that the last request was a cast.
							// If it indeed is a cast, the game UI will block the user from generating additional requests,
							// so first item is guaranteed to be the cast action.
							if (!m_pendingActions.empty())
								m_pendingActions.front().CastFlag = true;

							if (runtimeConfig.UseHighLatencyMitigationLogging)
								Misc::Logger::GetLogger().Format(
									LogCategory::AnimationLockLatencyHandler,
									"%zx: S2C_ActorCast: actionId=%04x time=%.3f target=%08x",
									conn.GetSocket(),
									actorCast.ActionId,
									actorCast.CastTime,
									actorCast.TargetId);
						}
					}
				}
				return true;
				});
		}
		~SingleConnectionHandler() {
			conn.RemoveMessageHandlers(this);
		}
	};

	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers;

	Internals() {
		Network::SocketHook::Instance()->AddOnSocketFoundListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(*this, conn));
			});
		Network::SocketHook::Instance()->AddOnSocketGoneListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.erase(&conn);
			});
	}

	~Internals() {
		m_handlers.clear();
		Network::SocketHook::Instance()->RemoveListeners(this);
	}
};

App::Feature::AnimationLockLatencyHandler::AnimationLockLatencyHandler()
	: impl(std::make_unique<Internals>()) {
}

App::Feature::AnimationLockLatencyHandler::~AnimationLockLatencyHandler() = default;
