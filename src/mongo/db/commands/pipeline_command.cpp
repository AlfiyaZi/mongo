/**
 * Copyright (c) 2011-2014 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects for
 * all of the code used other than as permitted herein. If you modify file(s)
 * with this exception, you may extend this exception to your version of the
 * file(s), but you are not obligated to do so. If you do not wish to do so,
 * delete this exception statement from your version. If you delete this
 * exception statement from all source files in the program, then also delete
 * it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/commands.h"
#include "mongo/db/commands/run_aggregate.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/pipeline/pipeline.h"
#include "mongo/db/server_options.h"

namespace mongo {
namespace {

bool isMergePipeline(const std::vector<BSONObj>& pipeline) {
    if (pipeline.empty()) {
        return false;
    }
    return pipeline[0].hasField("$mergeCursors");
}

class PipelineCommand : public Command {
public:
    PipelineCommand() : Command("aggregate") {}

    void help(std::stringstream& help) const override {
        help << "Runs the aggregation command. See http://dochub.mongodb.org/core/aggregation for "
                "more details.";
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return Pipeline::aggSupportsWriteConcern(cmd);
    }

    bool slaveOk() const override {
        return false;
    }

    bool slaveOverrideOk() const override {
        return true;
    }

    bool supportsReadConcern() const override {
        return true;
    }

    ReadWriteType getReadWriteType() const {
        return ReadWriteType::kRead;
    }

    Status checkAuthForCommand(Client* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) override {
        const NamespaceString nss(parseNsCollectionRequired(dbname, cmdObj));
        return AuthorizationSession::get(client)->checkAuthForAggregate(nss, cmdObj);
    }

    bool run(OperationContext* opCtx,
             const std::string& dbname,
             const BSONObj& cmdObj,
             std::string& errmsg,
             BSONObjBuilder& result) override {
        return appendCommandStatus(result,
                                   _runAggCommand(opCtx, dbname, cmdObj, boost::none, &result));
    }

    Status explain(OperationContext* opCtx,
                   const std::string& dbname,
                   const BSONObj& cmdObj,
                   ExplainOptions::Verbosity verbosity,
                   BSONObjBuilder* out) const override {
        return _runAggCommand(opCtx, dbname, cmdObj, verbosity, out);
    }

private:
    static Status _runAggCommand(OperationContext* opCtx,
                                 const std::string& dbname,
                                 const BSONObj& cmdObj,
                                 boost::optional<ExplainOptions::Verbosity> verbosity,
                                 BSONObjBuilder* result) {
        const NamespaceString nss(parseNsCollectionRequired(dbname, cmdObj));

        const auto aggregationRequest =
            uassertStatusOK(AggregationRequest::parseFromBSON(nss, cmdObj, verbosity));

        // If the featureCompatibilityVersion is 3.2, we disallow collation from the user. However,
        // operations should still respect the collection default collation. The mongos attaches the
        // collection default collation to the merger pipeline, since the merger may not have the
        // collection metadata. So the merger needs to accept a collation, and we rely on the shards
        // to reject collations from the user.
        uassert(ErrorCodes::InvalidOptions,
                "The featureCompatibilityVersion must be 3.4 to use collation. See "
                "http://dochub.mongodb.org/core/3.4-feature-compatibility.",
                aggregationRequest.getCollation().isEmpty() ||
                    serverGlobalParams.featureCompatibility.version.load() !=
                        ServerGlobalParams::FeatureCompatibility::Version::k32 ||
                    isMergePipeline(aggregationRequest.getPipeline()));

        return runAggregate(opCtx, nss, aggregationRequest, cmdObj, *result);
    }

} pipelineCmd;

}  // namespace
}  // namespace mongo
