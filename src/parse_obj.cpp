#include "parse_obj.h"

UINT32 parse_floats(char** char_buf, UINT32 max_count, float* out) {
    char* substr = *char_buf;
    int i = 0;
    while (i < max_count) {
        // skip leading whitespace
        while (isspace(substr[0])) {
            if (substr[0] == '\r' || substr[0] == '\n') {
                substr = NULL;
                goto end;
            }
            substr += 1;
        }

        // parse the float and append to array
        out[i] = atof(substr);
        i += 1;

        // advance past the float literal
        while (!isspace(substr[0])) substr += 1;
    }
end:
    *char_buf = substr;
    return i;
}

void parse_filepath(char** char_buf, char* output) {
    char* filepath = *char_buf;
    char* reader = filepath;
    for (int i = 0; *reader != '\n'; i++) {
        if (*reader == '\0') {
            printf("error! end of file reached prematurely");
            free(output);
        }
        output[i] = *reader;
        reader++;
    }
}

void parse_mtl_file(const char* filename, Material* mat, char* img_filepath) {
    FILE* file = fopen(filename, "r");
    const UINT64 char_buf_len = 64;
    char* char_buf = (char*) malloc(char_buf_len * sizeof(char));

    float roughness;
    float refraction;

    while (!feof(file)) {
        char* line = fgets(char_buf, char_buf_len, file);
        int error = ferror(file);
        if (error) {
            fprintf(stderr, "error reading obj file");
            free(char_buf);
            exit(error);
        }
        if (!line) break; // end of file

        if (line[0] == 'N') {
            if (line[1] == 's') {   //read Ns
                assert(parse_floats(&line, 1, (float*) &roughness) == 1);
            }
            if (line[1] == 'i') {   //read Ni
                assert(parse_floats(&line, 1, (float*)&refraction) == 1);
            }
        }
        
        //read map_Kd
        if (strncmp(line, "map_Kd", 5) != 0) {
            //load file from file path
            img_filepath = (char*)malloc(256 * sizeof(char));
            parse_filepath(&line, img_filepath);
        }
    }
    free(char_buf);
    fclose(file);

    mat->info.roughness = roughness;
    mat->info.index_refraction = refraction;

    //free(filepath);
}

void parse_obj_file(const char* filename, bool convert_to_rhs, Array<Vertex>* vertices, Array<Index>* indices, Aabb* aabb) {
    FILE* file = fopen(filename, "rb");

    const UINT64 char_buf_len = 512;
    char* char_buf = (char*) malloc(char_buf_len * sizeof(char));

    Array<XMFLOAT3> vs  = {};
    Array<XMFLOAT2> vts = {};
    Array<XMFLOAT3> vns = {};

    while (!feof(file)) {
        // read file
        char* substr = fgets(char_buf, char_buf_len, file);
        int error = ferror(file);
        if (error) {
            fprintf(stderr, "error reading obj file");
            exit(error);
        }
        if (!substr) break; // end of file

        // parse
        if (substr[0] == 'v') {
            // vertex attribute
            if (isspace(substr[1])) {
                substr += 1;
                // position
                XMFLOAT3 v;

                assert(parse_floats(&substr, 3, (float*) &v) == 3);
                if (aabb) {
                    *aabb = aabb_join(*aabb, XMLoadFloat3(&v));
                }
                array_push(&vs, v);
            } else if (substr[1] == 't' && isspace(substr[2])) {
                substr += 2;
                // texture coordinate
                // NOTE: .obj spec supports 1-3 texture coordinates, this assumes 2
                XMFLOAT2 vt;
                assert(parse_floats(&substr, 2, (float*) &vt) == 2);
                array_push(&vts, vt);
            } else if (substr[1] == 'n' && isspace(substr[2])) {
                substr += 2;
                // normal
                XMFLOAT3 vn;
                assert(parse_floats(&substr, 3, (float*) &vn) == 3);
                array_push(&vns, vn);
            }
        } else if (substr[0] == 'f') {
            substr += 1;

            UINT32 v_indices [4]; UINT32 v_indices_count  = 0;
            UINT32 vt_indices[4]; UINT32 vt_indices_count = 0;
            UINT32 vn_indices[4]; UINT32 vn_indices_count = 0;

            int verts_count = 0;
            while (verts_count < 4) {
                // skip leading whitespace
                while (isspace(substr[0])) {
                    if (substr[0] == '\r' || substr[0] == '\n') {
                        substr = NULL;
                        goto end_face;
                    }
                    substr += 1;
                }
                verts_count += 1;

                // always expect a vertex point index
                v_indices[v_indices_count] = atoi(substr) - 1;
                v_indices_count += 1;

                while (substr[0] != '/') {
                    substr += 1;
                    if (isspace(substr[0])) goto end_vertex;
                }
                substr += 1;
                if (substr[0] != '/') {
                    // take texture coordinate after the first slash
                    vt_indices[vt_indices_count] = atoi(substr) - 1;
                    vt_indices_count += 1;

                    while (substr[0] != '/') {
                        substr += 1;
                        if (isspace(substr[0])) goto end_vertex;
                    }
                }
                substr += 1;

                // take vertex normal after second slash
                vn_indices[vn_indices_count] = atoi(substr) - 1;
                vn_indices_count += 1;

            end_vertex:
                // advance past this vertex
                while (!isspace(substr[0])) substr += 1;
            }
        end_face:
            assert(verts_count >= 3);
            assert(v_indices_count  == verts_count);
            assert(vt_indices_count == verts_count || vt_indices_count == 0);
            assert(vn_indices_count == verts_count || vn_indices_count == 0);

            // create the triangle indices
            Index base_index = vertices->len;
            if (verts_count == 3) {
                Index face_indices[] = {
                    (Index) (base_index+0), (Index) (base_index+1), (Index) (base_index+2)
                };
                array_concat(indices, &VLA_VIEW(face_indices));
            } else if (verts_count == 4) {
                Index face_indices[] = {
                    (Index) (base_index+0), (Index) (base_index+1), (Index) (base_index+2),
                    (Index) (base_index+0), (Index) (base_index+2), (Index) (base_index+3)
                };
                array_concat(indices, &VLA_VIEW(face_indices));
            } else {
                assert(false);
            }

            // create the vertices from the indexed data
            // TODO: deduplicate
            for (int i = 0; i < verts_count; i++) {
                Vertex vertex = {};
                vertex.position = vs[v_indices[i]];

                if (vn_indices_count) vertex.normal = vns[vn_indices[i]];
                else {
                    // create rudimentary face normals if vertex normals were not specified
                    // TODO: generate weighted vertex normals
                    XMVECTOR a = XMLoadFloat3(&vs[v_indices[0]]);
                    XMVECTOR b = XMLoadFloat3(&vs[v_indices[1]]) - a;
                    XMVECTOR c = XMLoadFloat3(&vs[v_indices[2]]) - a;
                    XMStoreFloat3(&vertex.normal, XMVector3Normalize(XMVector3Cross(b, c)));
                }
                if (convert_to_rhs) {
                    swap(&vertex.position.y, &vertex.position.z);
                    vertex.position.x = -vertex.position.x + XMVectorGetX(aabb->max + aabb->min);

                    swap(&vertex.normal.y, &vertex.normal.z);
                    vertex.normal.x = -vertex.normal.x;
                }
                // if (vt_indices_count); // TODO: handle uv coordinates
                if (vt_indices_count) vertex.uv = vts[vt_indices[i]];
                array_push(vertices, vertex);
            }
        }
    }
    if (convert_to_rhs) {
        aabb->min = XMVectorSwizzle<0, 2, 1, 3>(aabb->min);
        aabb->max = XMVectorSwizzle<0, 2, 1, 3>(aabb->max);
    }
    free(char_buf);
    fclose(file);
}