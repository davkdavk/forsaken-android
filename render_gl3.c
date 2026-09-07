#if GL == 3
#include "render_gl_shared.h"

bool FSCreateVertexBuffer(RENDEROBJECT *renderObject, int numVertices)
{
	renderObject->lpVertexBuffer = create_buffer( numVertices * sizeof(LVERTEX), GL_ARRAY_BUFFER, GL_STATIC_DRAW );
	return true;
}
bool FSCreateDynamicVertexBuffer(RENDEROBJECT *renderObject, int numVertices)
{
	renderObject->lpVertexBuffer = create_buffer( numVertices * sizeof(LVERTEX), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW );
	return true;
}

bool FSCreateNormalBuffer(RENDEROBJECT *renderObject, int numNormals)
{
	renderObject->lpNormalBuffer = create_buffer( numNormals * sizeof(NORMAL), GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW );
	return true;
}
bool FSCreateDynamicNormalBuffer(RENDEROBJECT *renderObject, int numNormals)
{
	renderObject->lpNormalBuffer = create_buffer( numNormals * sizeof(NORMAL), GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW );
	return true;
}

bool FSCreateIndexBuffer(RENDEROBJECT *renderObject, int numIndices)
{
	renderObject->lpIndexBuffer = create_buffer( numIndices * 3 * sizeof(WORD), GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW );
	return true;
}
bool FSCreateDynamicIndexBuffer(RENDEROBJECT *renderObject, int numIndices)
{
	renderObject->lpIndexBuffer = create_buffer( numIndices * 3 * sizeof(WORD), GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW );
	return true;
}

// In OpenGL you can only map buffers currently bound to a predefined
// buffer binding point ("target"), so we save the currently bound
// buffer and restore it on unlock.
//
// There is still the restriction that only one buffer of a certain
// type may be locked at the same time. If this is a problem then this
// should be rewritten to use a locally malloc'ed buffer and update
// the real buffer on unlock using glBufferSubData.

static GLuint old_array_buf = 0;
static GLuint old_index_buf = 0;
#ifdef RENDER_GLES
static GLuint g_vao = 0;
#endif

#ifdef RENDER_GLES
// GLES3 has no glMapBuffer; emulate a whole-buffer write mapping
static void * map_buffer_write( GLenum target )
{
	GLint size = 0;
	glGetBufferParameteriv( target, GL_BUFFER_SIZE, &size );
	return glMapBufferRange( target, 0, size, GL_MAP_WRITE_BIT );
}
#else
#define map_buffer_write( target ) glMapBuffer( target, GL_WRITE_ONLY )
#endif

bool FSLockVertexBuffer(RENDEROBJECT *renderObject, LVERTEX **verts)
{
	if ( old_array_buf )
	{
		DebugPrintf( "Tried to lock more than one vertex buffer at once\n" );
		return false;
	}
	glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &old_array_buf );
	glBindBuffer( GL_ARRAY_BUFFER, (GLuint) renderObject->lpVertexBuffer );
	*verts = (LVERTEX *) map_buffer_write( GL_ARRAY_BUFFER );
	if(!*verts)
	{
		DebugPrintf("FSLockVertexBuffer: glMapBuffer returned NULL\n");
		return false;
	}
	CHECK_GL_ERRORS;
	return true;
}

bool FSUnlockVertexBuffer(RENDEROBJECT *renderObject)
{
	bool ret = ( glUnmapBuffer( GL_ARRAY_BUFFER ) == GL_TRUE );
	glBindBuffer( GL_ARRAY_BUFFER, old_array_buf );
	old_array_buf = 0;
	CHECK_GL_ERRORS;
	return ret;
}

bool FSLockNormalBuffer(RENDEROBJECT *renderObject, NORMAL **normals)
{
	if ( old_array_buf )
	{
		DebugPrintf( "Tried to lock more than one vertex buffer at once\n" );
		return false;
	}
	glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &old_array_buf );
	glBindBuffer( GL_ARRAY_BUFFER, (GLuint) renderObject->lpNormalBuffer );
	*normals = (NORMAL *) map_buffer_write( GL_ARRAY_BUFFER );
	if(!*normals)
	{
		DebugPrintf("FSLockNormalBuffer: glMapBuffer returned NULL\n");
		return false;
	}
	CHECK_GL_ERRORS;
	return true;
}

bool FSUnlockNormalBuffer(RENDEROBJECT *renderObject)
{
	bool ret = ( glUnmapBuffer( GL_ARRAY_BUFFER ) == GL_TRUE );
	glBindBuffer( GL_ARRAY_BUFFER, old_array_buf );
	old_array_buf = 0;
	CHECK_GL_ERRORS;
	return ret;
}

bool FSLockIndexBuffer(RENDEROBJECT *renderObject, WORD **indices)
{
	if ( old_index_buf )
	{
		DebugPrintf( "Tried to lock more than one index buffer at once\n" );
		return false;
	}
	glGetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING, &old_index_buf );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, (GLuint) renderObject->lpIndexBuffer );
	*indices = (WORD *) map_buffer_write( GL_ELEMENT_ARRAY_BUFFER );
	if(!*indices)
	{
		DebugPrintf("FSLockIndexBuffer: glMapBuffer returned NULL\n");
		return false;
	}
	CHECK_GL_ERRORS;
	return true;
}

bool FSUnlockIndexBuffer(RENDEROBJECT *renderObject)
{
	bool ret = ( glUnmapBuffer( GL_ELEMENT_ARRAY_BUFFER ) == GL_TRUE );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, old_index_buf );
	old_index_buf = 0;
	CHECK_GL_ERRORS;
	return ret;
}

bool FSCreateDynamic2dVertexBuffer(RENDEROBJECT *renderObject, int numVertices)
{
	renderObject->lpVertexBuffer = create_buffer( numVertices * sizeof(TLVERTEX), GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW );
	return true;
}

bool FSLockPretransformedVertexBuffer(RENDEROBJECT *renderObject, TLVERTEX **verts)
{
	return FSLockVertexBuffer( renderObject, (LVERTEX **) verts );
}

/* Draw render object:
 * - if 2D (orthographic), set up appropriately:
 *   - orthographic projection matrix
 *   - ... plus scaling and translation for Y-flipping (T*S*P)
 *   else:
 *   - update mvp if necessary (mvp_needs_update)
 * - for each texture group (renderObject->numTextureGroups)
 *   - group = &renderObject->textureGroups[i]
 *   - if group->colourkey, enable color-keying
 *   - if group->texture, enable texturing and bind
 *     renderObject->textureGroups[group].texture
 *   - draw group->numVerts elements starting at group->startVert
 */

bool draw_render_object( RENDEROBJECT *renderObject, int primitive_type, bool orthographic )
{
	static const struct
	{
		const char *name;
		int components;
		GLenum type;
		GLboolean normalized;
		int offset;
	} normal_attr[] =
	{
		{ "pos",    3, GL_FLOAT,         GL_FALSE, 0  },
		{ "vcolor", 4, GL_UNSIGNED_BYTE, GL_TRUE,  12 }, // 3*float
		{ "vtexc",  2, GL_FLOAT,         GL_FALSE, 16 }, // 3*float + 1*COLOR
		{ NULL,     0, 0,                0,        0  }
	}, ortho_attr[] =
	{
		{ "tlpos",  4, GL_FLOAT,         GL_FALSE, 0 },
		{ "vcolor", 4, GL_UNSIGNED_BYTE, GL_TRUE,  16 }, // 4*float
		{ "vtexc",  2, GL_FLOAT,         GL_FALSE, 20 }, // 4*float + 1*COLOR
		{ NULL,     0, 0,                0,        0  }
	}, *attr;
	//GLuint current_program;
	GLint u_ortho;
	GLint u_colorkey;
	GLint u_enabletex;
	TEXTUREGROUP *group;
	texture_t *texdata;
	int loc;
	int i;
	int __ai;

	glBindBuffer( GL_ARRAY_BUFFER, (GLuint)(size_t) renderObject->lpVertexBuffer );

	if ( renderObject->lpIndexBuffer )
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, (GLuint)(size_t) renderObject->lpIndexBuffer );
	else
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

	//glGetIntegerv( GL_CURRENT_PROGRAM, &current_program );
	// assert( current_program != 0 );

	CHECK_GL_ERRORS;

	// Tell OpenGL about the buffer data layout
	// see the LVERTEX and TLVERTEX definitions inside include/new3d.h
	// base_vertex shifts the attribute base so indices can stay relative
	// (used to emulate glDrawElementsBaseVertex on GLES < 3.2)
	attr = orthographic ? ortho_attr : normal_attr;
	{
		size_t stride = orthographic ? sizeof(TLVERTEX) : sizeof(LVERTEX);
		size_t base = 0;
		/* Attribute locations are string lookups inside the driver. They
		 * depend only on the linked program, so resolve them once here
		 * instead of on every SETUP_ATTRIBS() call (which the GLES
		 * BaseVertex emulation invokes per texture group). */
		int attr_loc[8];
		int normal_loc = -1;
		{
			int k;
			for ( k = 0; k < 8 && attr[k].name; k++ )
				attr_loc[k] = glGetAttribLocation( current_program, attr[k].name );
			for ( ; k < 8; k++ )
				attr_loc[k] = -1;
			if ( renderObject->lpNormalBuffer )
				normal_loc = glGetAttribLocation( current_program, "vnormal" );
		}
#define SETUP_ATTRIBS( base_vertex ) \
	do { \
		base = (size_t)(base_vertex); \
		glBindBuffer( GL_ARRAY_BUFFER, (GLuint)(size_t) renderObject->lpVertexBuffer ); \
		for ( __ai=0; attr[__ai].name; __ai++ ) \
		{ \
			loc = attr_loc[__ai]; \
			if (loc >= 0) \
			{ \
				glVertexAttribPointer( \
					loc, \
					attr[__ai].components, \
					attr[__ai].type, \
					attr[__ai].normalized, \
					stride, \
					(const GLvoid *)( base * stride + (size_t) attr[__ai].offset ) \
				); \
				glEnableVertexAttribArray( loc ); \
			} \
		} \
		if ( renderObject->lpNormalBuffer ) \
		{ \
			glBindBuffer( GL_ARRAY_BUFFER, (GLuint)(size_t) renderObject->lpNormalBuffer ); \
			loc = normal_loc; \
			if (loc >= 0) \
			{ \
				glVertexAttribPointer( loc, 3, GL_FLOAT, GL_FALSE, sizeof(NORMAL), \
					(const GLvoid *)( base * sizeof(NORMAL) ) ); \
				glEnableVertexAttribArray( loc ); \
			} \
		} \
	} while (0)

 #ifdef RENDER_GLES
        if (g_vao == 0) glGenVertexArrays(1, &g_vao);
        glBindVertexArray(g_vao);
 #endif
	SETUP_ATTRIBS( 0 );

	CHECK_GL_ERRORS;

	// Update and use the appropriate model/view/projection matrix
	if ( orthographic )
		ortho_update( current_program );
	else
		mvp_update( current_program );

	// This uniform tells the vertex shader which matrix to use
	if ((u_ortho = glGetUniformLocation(current_program, "orthographic")) >= 0)
		glUniform1i( u_ortho, orthographic ? GL_TRUE : GL_FALSE );

	/* Locations depend only on the program, not on the group. */
	u_colorkey  = glGetUniformLocation( current_program, "colorkeying_enabled" );
	u_enabletex = glGetUniformLocation( current_program, "texturing_enabled" );

	for ( i = 0; i < renderObject->numTextureGroups; i++ )
	{
		group = &renderObject->textureGroups[i];
		if ( u_colorkey >= 0 )
			glUniform1i( u_colorkey, group->colourkey ? GL_TRUE : GL_FALSE );
		if ( u_enabletex >= 0 )
		{
			glUniform1i( u_enabletex, group->texture ? GL_TRUE : GL_FALSE );
			if ( group->texture )
			{
				texdata = (texture_t *) group->texture;
				glBindTexture( GL_TEXTURE_2D, texdata->id );
			}
		}
#ifdef RENDER_GLES
		// emulate BaseVertex: rebase the attribute pointers instead
		if ( (size_t) group->startVert != base )
			SETUP_ATTRIBS( group->startVert );
		glDrawElements( primitive_type, group->numTriangles * 3, GL_UNSIGNED_SHORT,
			(const GLvoid *)( (size_t) group->startIndex * sizeof(WORD) ) );
#else
		glDrawElementsBaseVertex( primitive_type, group->numTriangles * 3, GL_UNSIGNED_SHORT, group->startIndex * sizeof(WORD), group->startVert );
#endif
	}
	} // stride/base scope
#undef SETUP_ATTRIBS

	CHECK_GL_ERRORS;

	for ( i=0; attr[i].name; i++ )
	{
		loc = glGetAttribLocation( current_program, attr[i].name );
		if (loc >= 0)
			glDisableVertexAttribArray( loc );
	}

	if ( renderObject->lpNormalBuffer )
	{
		loc = glGetAttribLocation( current_program, "vnormal" );
		if (loc >= 0)
			glDisableVertexAttribArray( loc );
	}

	CHECK_GL_ERRORS;

 #ifdef RENDER_GLES
    glBindVertexArray(0);
 #endif

	CHECK_GL_ERRORS;

	return true;
}

#endif // GL == 3
