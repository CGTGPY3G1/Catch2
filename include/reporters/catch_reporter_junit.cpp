/*
 *  Created by Phil on 26/11/2010.
 *  Copyright 2010 Two Blue Cubes Ltd. All rights reserved.
 *
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#include "catch_reporter_bases.hpp"

#include "catch_reporter_junit.h"

#include "../internal/catch_tostring.h"
#include "../internal/catch_reporter_registrars.hpp"

#include <assert.h>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace Catch {

    namespace {
        std::string getCurrentTimestamp() {
            // Beware, this is not reentrant because of backward compatibility issues
            // Also, UTC only, again because of backward compatibility (%z is C++11)
            time_t rawtime;
            std::time(&rawtime);
            auto const timeStampSize = sizeof("2017-01-16T17:06:45Z");

#ifdef _MSC_VER
            std::tm timeInfo = {};
            gmtime_s(&timeInfo, &rawtime);
#else
            std::tm* timeInfo;
            timeInfo = std::gmtime(&rawtime);
#endif

            char timeStamp[timeStampSize];
            const char * const fmt = "%Y-%m-%dT%H:%M:%SZ";

#ifdef _MSC_VER
            std::strftime(timeStamp, timeStampSize, fmt, &timeInfo);
#else
            std::strftime(timeStamp, timeStampSize, fmt, timeInfo);
#endif
            return std::string(timeStamp);
        }

        std::string fileNameTag(const std::vector<std::string> &tags) {
            auto it = std::find_if(begin(tags),
                                   end(tags),
                                   [] (std::string const& tag) {return tag.front() == '#'; });
            if (it != tags.end())
                return it->substr(1);
            return std::string();
        }
    } // anonymous namespace

    JunitReporter::JunitReporter( ReporterConfig const& _config )
        :   CumulativeReporterBase( _config ),
            xml( _config.stream() )
        {
            m_reporterPrefs.shouldRedirectStdOut = true;
        }

    JunitReporter::~JunitReporter() {}

    std::string JunitReporter::getDescription() {
        return "Reports test results in an XML format that looks like Ant's junitreport target";
    }

    void JunitReporter::noMatchingTestCases( std::string const& /*spec*/ ) {}

    void JunitReporter::testRunStarting( TestRunInfo const& runInfo )  {
        CumulativeReporterBase::testRunStarting( runInfo );
        xml.startElement( "testsuites" );
    }

    void JunitReporter::testGroupStarting( GroupInfo const& groupInfo ) {
        suiteTimer.start();
        stdOutForSuite.clear();
        stdErrForSuite.clear();
        unexpectedExceptions = 0;
        CumulativeReporterBase::testGroupStarting( groupInfo );
    }

    void JunitReporter::testCaseStarting( TestCaseInfo const& testCaseInfo ) {
        m_okToFail = testCaseInfo.okToFail();
    }

    bool JunitReporter::assertionEnded( AssertionStats const& assertionStats ) {
        if( assertionStats.assertionResult.getResultType() == ResultWas::ThrewException && !m_okToFail )
            unexpectedExceptions++;
        return CumulativeReporterBase::assertionEnded( assertionStats );
    }

    void JunitReporter::testCaseEnded( TestCaseStats const& testCaseStats ) {
        stdOutForSuite += testCaseStats.stdOut;
        stdErrForSuite += testCaseStats.stdErr;
        CumulativeReporterBase::testCaseEnded( testCaseStats );
    }

    void JunitReporter::testGroupEnded( TestGroupStats const& testGroupStats ) {
        double suiteTime = suiteTimer.getElapsedSeconds();
        CumulativeReporterBase::testGroupEnded( testGroupStats );
        writeGroup( *m_testGroups.back(), suiteTime );
    }

    void JunitReporter::testRunEndedCumulative() {
        xml.endElement();
    }

    void JunitReporter::writeGroup( TestGroupNode const& groupNode, double suiteTime ) {
        XmlWriter::ScopedElement e = xml.scopedElement( "testsuite" );
        TestGroupStats const& stats = groupNode.value;
        xml.writeAttribute( "name", stats.groupInfo.name );
        xml.writeAttribute( "errors", unexpectedExceptions );
        xml.writeAttribute( "failures", stats.totals.assertions.failed-unexpectedExceptions );
        xml.writeAttribute( "tests", stats.totals.assertions.total() );
        xml.writeAttribute( "hostname", "tbd" ); // !TBD
        if( m_config->showDurations() == ShowDurations::Never )
            xml.writeAttribute( "time", "" );
        else
            xml.writeAttribute( "time", suiteTime );
        xml.writeAttribute( "timestamp", getCurrentTimestamp() );

        // Write test cases
        for( auto const& child : groupNode.children )
            writeTestCase( *child );

        xml.scopedElement( "system-out" ).writeText( trim( stdOutForSuite ), XmlFormatting::None );
        xml.scopedElement( "system-err" ).writeText( trim( stdErrForSuite ), XmlFormatting::None );
    }

    void JunitReporter::writeTestCase( TestCaseNode const& testCaseNode ) {
        TestCaseStats const& stats = testCaseNode.value;

        // All test cases have exactly one section - which represents the
        // test case itself. That section may have 0-n nested sections
        assert( testCaseNode.children.size() == 1 );
        SectionNode const& rootSection = *testCaseNode.children.front();

        std::string className = stats.testInfo.className;

        if( className.empty() ) {
            className = fileNameTag(stats.testInfo.tags);
            if ( className.empty() )
                className = "global";
        }

        if ( !m_config->name().empty() )
            className = m_config->name() + "." + className;

        writeSection( className, "", rootSection );
    }

    void JunitReporter::writeSection(  std::string const& className,
                        std::string const& rootName,
                        SectionNode const& sectionNode ) {
        std::string name = trim( sectionNode.stats.sectionInfo.name );
        if( !rootName.empty() )
            name = rootName + '/' + name;

        if( !sectionNode.assertions.empty() ||
            !sectionNode.stdOut.empty() ||
            !sectionNode.stdErr.empty() ) {
            XmlWriter::ScopedElement e = xml.scopedElement( "testcase" );
            if( className.empty() ) {
                xml.writeAttribute( "classname", name );
                xml.writeAttribute( "name", "root" );
            }
            else {
                xml.writeAttribute( "classname", className );
                xml.writeAttribute( "name", name );
            }
            xml.writeAttribute( "time", ::Catch::Detail::stringify( sectionNode.stats.durationInSeconds ) );

            writeAssertions( sectionNode );

            if( !sectionNode.stdOut.empty() )
                xml.scopedElement( "system-out" ).writeText( trim( sectionNode.stdOut ), XmlFormatting::None );
            if( !sectionNode.stdErr.empty() )
                xml.scopedElement( "system-err" ).writeText( trim( sectionNode.stdErr ), XmlFormatting::None );
        }
        for( auto const& childNode : sectionNode.childSections )
            if( className.empty() )
                writeSection( name, "", *childNode );
            else
                writeSection( className, name, *childNode );
    }

    void JunitReporter::writeAssertions( SectionNode const& sectionNode ) {
        for( auto const& assertion : sectionNode.assertions )
            writeAssertion( assertion );
    }

    void JunitReporter::writeAssertion( AssertionStats const& stats ) {
        AssertionResult const& result = stats.assertionResult;
        if( !result.isOk() ) {
            std::string elementName;
            switch( result.getResultType() ) {
                case ResultWas::ThrewException:
                case ResultWas::FatalErrorCondition:
                    elementName = "error";
                    break;
                case ResultWas::ExplicitFailure:
                    elementName = "failure";
                    break;
                case ResultWas::ExpressionFailed:
                    elementName = "failure";
                    break;
                case ResultWas::DidntThrowException:
                    elementName = "failure";
                    break;

                // We should never see these here:
                case ResultWas::Info:
                case ResultWas::Warning:
                case ResultWas::Ok:
                case ResultWas::Unknown:
                case ResultWas::FailureBit:
                case ResultWas::Exception:
                    elementName = "internalError";
                    break;
            }

            XmlWriter::ScopedElement e = xml.scopedElement( elementName );

            xml.writeAttribute( "message", result.getExpandedExpression() );
            xml.writeAttribute( "type", result.getTestMacroName() );

            ReusableStringStream rss;
            if( !result.getMessage().empty() )
                rss << result.getMessage() << '\n';
            for( auto const& msg : stats.infoMessages )
                if( msg.type == ResultWas::Info )
                    rss << msg.message << '\n';

            rss << "at " << result.getSourceInfo();
            xml.writeText( rss.str(), XmlFormatting::None );
        }
    }

    CATCH_REGISTER_REPORTER( "junit", JunitReporter )

} // end namespace Catch
