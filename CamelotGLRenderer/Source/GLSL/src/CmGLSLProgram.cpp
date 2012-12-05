/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2011 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "CmGpuProgram.h"
#include "CmRenderSystem.h"
#include "CmRenderSystemCapabilities.h"
#include "CmGpuProgramManager.h"
#include "CmHighLevelGpuProgramManager.h"
#include "CmException.h"

#include "CmGLSLProgram.h"
#include "CmGLSLGpuProgram.h"
#include "CmGLSLExtSupport.h"
#include "CmGLSLLinkProgramManager.h"
#include "CmGLSLPreprocessor.h"

#include "CmGLSLProgramRTTI.h"

namespace CamelotEngine {
    //---------------------------------------------------------------------------
    GLSLProgram::~GLSLProgram()
    {
        unload();
    }
	//-----------------------------------------------------------------------
	GLSLProgram::GLSLProgram(const String& source, const String& entryPoint, const String& language, 
		GpuProgramType gptype, GpuProgramProfile profile, bool isAdjacencyInfoRequired)
		: HighLevelGpuProgram(source, entryPoint, language, gptype, profile, isAdjacencyInfoRequired),
		mInputOperationType(RenderOperation::OT_TRIANGLE_LIST),
		mOutputOperationType(RenderOperation::OT_TRIANGLE_LIST), mMaxOutputVertices(3)
	{
		// Manually assign language now since we use it immediately
		mSyntaxCode = "glsl";

	}
    //-----------------------------------------------------------------------
	void GLSLProgram::loadFromSource(void)
	{
		// only create a shader object if glsl is supported
		if (isSupported())
		{
			checkForGLSLError( "GLSLProgram::loadFromSource", "GL Errors before creating shader object", 0 );
			// create shader object

			GLenum shaderType = 0x0000;
			switch (mType)
			{
			case GPT_VERTEX_PROGRAM:
				shaderType = GL_VERTEX_SHADER_ARB;
				break;
			case GPT_FRAGMENT_PROGRAM:
				shaderType = GL_FRAGMENT_SHADER_ARB;
				break;
			case GPT_GEOMETRY_PROGRAM:
				shaderType = GL_GEOMETRY_SHADER_EXT;
				break;
			}
			mGLHandle = glCreateShaderObjectARB(shaderType);

			checkForGLSLError( "GLSLProgram::loadFromSource", "Error creating GLSL shader object", 0 );
		}

		// Preprocess the GLSL shader in order to get a clean source
		CPreprocessor cpp;

		// Pass all user-defined macros to preprocessor
		if (!mPreprocessorDefines.empty ())
		{
			String::size_type pos = 0;
			while (pos != String::npos)
			{
				// Find delims
				String::size_type endPos = mPreprocessorDefines.find_first_of(";,=", pos);
				if (endPos != String::npos)
				{
					String::size_type macro_name_start = pos;
					size_t macro_name_len = endPos - pos;
					pos = endPos;

					// Check definition part
					if (mPreprocessorDefines[pos] == '=')
					{
						// set up a definition, skip delim
						++pos;
						String::size_type macro_val_start = pos;
						size_t macro_val_len;

						endPos = mPreprocessorDefines.find_first_of(";,", pos);
						if (endPos == String::npos)
						{
							macro_val_len = mPreprocessorDefines.size () - pos;
							pos = endPos;
						}
						else
						{
							macro_val_len = endPos - pos;
							pos = endPos+1;
						}
						cpp.Define (
							mPreprocessorDefines.c_str () + macro_name_start, macro_name_len,
							mPreprocessorDefines.c_str () + macro_val_start, macro_val_len);
					}
					else
					{
						// No definition part, define as "1"
						++pos;
						cpp.Define (
							mPreprocessorDefines.c_str () + macro_name_start, macro_name_len, 1);
					}
				}
				else
					pos = endPos;
			}
		}

		size_t out_size = 0;
		const char *src = mSource.c_str ();
		size_t src_len = mSource.size ();
		char *out = cpp.Parse (src, src_len, out_size);
		if (!out || !out_size)
		{
			// Failed to preprocess, break out
			CM_EXCEPT(RenderingAPIException,
						 "Failed to preprocess shader ");
		}

		mSource = String (out, out_size);
		if (out < src || out > src + src_len)
			free (out);

		// Add preprocessor extras and main source
		if (!mSource.empty())
		{
			const char *source = mSource.c_str();
			glShaderSourceARB(mGLHandle, 1, &source, NULL);
			// check for load errors
			checkForGLSLError( "GLSLProgram::loadFromSource", "Cannot load GLSL high-level shader source : ", 0 );
		}

		compile();
	}
    
    //---------------------------------------------------------------------------
	bool GLSLProgram::compile(const bool checkErrors)
	{
        if (checkErrors)
        {
            logObjectInfo("GLSL compiling: ", mGLHandle);
        }

		glCompileShaderARB(mGLHandle);
		// check for compile errors
		glGetObjectParameterivARB(mGLHandle, GL_OBJECT_COMPILE_STATUS_ARB, &mCompiled);
		// force exception if not compiled
		if (checkErrors)
		{
			checkForGLSLError( "GLSLProgram::compile", "Cannot compile GLSL high-level shader : ", mGLHandle, !mCompiled, !mCompiled );
			
			if (mCompiled)
			{
				logObjectInfo("GLSL compiled : ", mGLHandle);
			}
		}
		return (mCompiled == 1);

	}

	//-----------------------------------------------------------------------
	void GLSLProgram::createLowLevelImpl(void)
	{
		mAssemblerProgram = GpuProgramPtr(new GLSLGpuProgram( this, mSource, mEntryPoint, mSyntaxCode, mType, mProfile));
	}
	//---------------------------------------------------------------------------
	void GLSLProgram::unloadImpl()
	{   
		// We didn't create mAssemblerProgram through a manager, so override this
		// implementation so that we don't try to remove it from one. Since getCreator()
		// is used, it might target a different matching handle!
		mAssemblerProgram = nullptr;

		unloadHighLevel();
	}
	//-----------------------------------------------------------------------
	void GLSLProgram::unloadHighLevelImpl(void)
	{
		if (isSupported())
		{
			glDeleteObjectARB(mGLHandle);
		}
	}

	//-----------------------------------------------------------------------
	void GLSLProgram::populateParameterNames(GpuProgramParametersSharedPtr params)
	{
		getConstantDefinitions_internal();
		params->_setNamedConstants(mConstantDefs);
		// Don't set logical / physical maps here, as we can't access parameters by logical index in GLHL.
	}
	//-----------------------------------------------------------------------
	void GLSLProgram::buildConstantDefinitions() const
	{
		// We need an accurate list of all the uniforms in the shader, but we
		// can't get at them until we link all the shaders into a program object.


		// Therefore instead, parse the source code manually and extract the uniforms
		createParameterMappingStructures(true);
		GLSLLinkProgramManager::instance().extractConstantDefs(
			mSource, *mConstantDefs.get(), "");

		// Also parse any attached sources
		for (GLSLProgramContainer::const_iterator i = mAttachedGLSLPrograms.begin();
			i != mAttachedGLSLPrograms.end(); ++i)
		{
			GLSLProgram* childShader = *i;

			GLSLLinkProgramManager::instance().extractConstantDefs(
				childShader->getSource(), *mConstantDefs.get(), "");

		}
	}
	//---------------------------------------------------------------------
	bool GLSLProgram::getPassSurfaceAndLightStates(void) const
	{
		// scenemanager should pass on light & material state to the rendersystem
		return true;
	}
	//---------------------------------------------------------------------
	bool GLSLProgram::getPassTransformStates(void) const
	{
		// scenemanager should pass on transform state to the rendersystem
		return true;
	}

	//-----------------------------------------------------------------------
    void GLSLProgram::attachChildShader(const String& name)
	{
		// is the name valid and already loaded?
		// check with the high level program manager to see if it was loaded
		// TODO PORT - I'm not sure what is this for but I don't support it
		//HighLevelGpuProgramPtr hlProgram = HighLevelGpuProgramManager::getSingleton().getByName(name);
		//if (!hlProgram.isNull())
		//{
		//	if (hlProgram->getSyntaxCode() == "glsl")
		//	{
		//		// make sure attached program source gets loaded and compiled
		//		// don't need a low level implementation for attached shader objects
		//		// loadHighLevelImpl will only load the source and compile once
		//		// so don't worry about calling it several times
		//		GLSLProgram* childShader = static_cast<GLSLProgram*>(hlProgram.getPointer());
		//		// load the source and attach the child shader only if supported
		//		if (isSupported())
		//		{
		//			childShader->loadHighLevelImpl();
		//			// add to the container
		//			mAttachedGLSLPrograms.push_back( childShader );
		//			mAttachedShaderNames += name + " ";
		//		}
		//	}
		//}
	}

	//-----------------------------------------------------------------------
	void GLSLProgram::attachToProgramObject( const GLhandleARB programObject )
	{
		// attach child objects
		GLSLProgramContainerIterator childprogramcurrent = mAttachedGLSLPrograms.begin();
		GLSLProgramContainerIterator childprogramend = mAttachedGLSLPrograms.end();

 		while (childprogramcurrent != childprogramend)
		{

			GLSLProgram* childShader = *childprogramcurrent;
			// bug in ATI GLSL linker : modules without main function must be recompiled each time 
			// they are linked to a different program object
			// don't check for compile errors since there won't be any
			// *** minor inconvenience until ATI fixes there driver
			childShader->compile(false);

			childShader->attachToProgramObject( programObject );

			++childprogramcurrent;
		}
		glAttachObjectARB( programObject, mGLHandle );
		checkForGLSLError( "GLSLProgram::attachToProgramObject",
			"Error attaching shader object to GLSL Program Object", programObject );

	}
	//-----------------------------------------------------------------------
	void GLSLProgram::detachFromProgramObject( const GLhandleARB programObject )
	{
		glDetachObjectARB(programObject, mGLHandle);
		checkForGLSLError( "GLSLProgram::detachFromProgramObject",
			"Error detaching shader object from GLSL Program Object", programObject );
		// attach child objects
		GLSLProgramContainerIterator childprogramcurrent = mAttachedGLSLPrograms.begin();
		GLSLProgramContainerIterator childprogramend = mAttachedGLSLPrograms.end();

		while (childprogramcurrent != childprogramend)
		{
			GLSLProgram* childShader = *childprogramcurrent;
			childShader->detachFromProgramObject( programObject );
			++childprogramcurrent;
		}

	}

    //-----------------------------------------------------------------------
    const String& GLSLProgram::getLanguage(void) const
    {
        static const String language = "glsl";

        return language;
    }

	/************************************************************************/
	/* 								SERIALIZATION                      		*/
	/************************************************************************/
	RTTITypeBase* GLSLProgram::getRTTIStatic()
	{
		return GLSLProgramRTTI::instance();
	}

	RTTITypeBase* GLSLProgram::getRTTI() const
	{
		return GLSLProgram::getRTTIStatic();
	}

	//-----------------------------------------------------------------------
	RenderOperation::OperationType parseOperationType(const String& val)
	{
		if (val == "point_list")
		{
			return RenderOperation::OT_POINT_LIST;
		}
		else if (val == "line_list")
		{
			return RenderOperation::OT_LINE_LIST;
		}
		else if (val == "line_strip")
		{
			return RenderOperation::OT_LINE_STRIP;
		}
		else if (val == "triangle_strip")
		{
			return RenderOperation::OT_TRIANGLE_STRIP;
		}
		else if (val == "triangle_fan")
		{
			return RenderOperation::OT_TRIANGLE_FAN;
		}
		else 
		{
			//Triangle list is the default fallback. Keep it this way?
			return RenderOperation::OT_TRIANGLE_LIST;
		}
	}
	//-----------------------------------------------------------------------
	String operationTypeToString(RenderOperation::OperationType val)
	{
		switch (val)
		{
		case RenderOperation::OT_POINT_LIST:
			return "point_list";
			break;
		case RenderOperation::OT_LINE_LIST:
			return "line_list";
			break;
		case RenderOperation::OT_LINE_STRIP:
			return "line_strip";
			break;
		case RenderOperation::OT_TRIANGLE_STRIP:
			return "triangle_strip";
			break;
		case RenderOperation::OT_TRIANGLE_FAN:
			return "triangle_fan";
			break;
		case RenderOperation::OT_TRIANGLE_LIST:
		default:
			return "triangle_list";
			break;
		}
	}
}
