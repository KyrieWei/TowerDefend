"""
Project Tools for Unreal MCP.

This module provides tools for managing project-wide settings and configuration.
"""

import logging
from typing import Dict, Any, List
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_project_tools(mcp: FastMCP):
    """Register project tools with the MCP server."""
    
    @mcp.tool()
    def create_input_mapping(
        ctx: Context,
        action_name: str,
        key: str,
        input_type: str = "Action"
    ) -> Dict[str, Any]:
        """
        Create an input mapping for the project.
        
        Args:
            action_name: Name of the input action
            key: Key to bind (SpaceBar, LeftMouseButton, etc.)
            input_type: Type of input mapping (Action or Axis)
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "action_name": action_name,
                "key": key,
                "input_type": input_type
            }
            
            logger.info(f"Creating input mapping '{action_name}' with key '{key}'")
            response = unreal.send_command("create_input_mapping", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Input mapping creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error creating input mapping: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def create_input_action(
        ctx: Context,
        action_name: str,
        path: str = "/Game/Input/Actions/",
        value_type: str = "Digital"
    ) -> Dict[str, Any]:
        """
        Create an Enhanced Input Action asset.
        
        Args:
            action_name: Name of the input action to create (e.g. IA_Jump)
            path: Content browser path where the action should be created
            value_type: Value type - Digital (bool), Axis1D (float), Axis2D (Vector2D), Axis3D (Vector)
            
        Returns:
            Response indicating success or failure with asset_path
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "action_name": action_name,
                "path": path,
                "value_type": value_type
            }
            
            logger.info(f"Creating input action '{action_name}' at '{path}' with value type '{value_type}'")
            response = unreal.send_command("create_input_action", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Create input action response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error creating input action: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_action_to_mapping_context(
        ctx: Context,
        mapping_context_path: str,
        input_action_path: str,
        key: str,
        negate: bool = False,
        swizzle: str = ""
    ) -> Dict[str, Any]:
        """
        Add an InputAction to an InputMappingContext with a key binding.
        If the MappingContext does not exist, it will be created automatically.
        
        Args:
            mapping_context_path: Asset path of the InputMappingContext (e.g. /Game/Input/IMC_Default)
            input_action_path: Asset path of the InputAction (e.g. /Game/Input/Actions/IA_Jump)
            key: Key to bind (e.g. SpaceBar, W, LeftMouseButton, RightMouseButton)
            negate: Whether to add a Negate modifier
            swizzle: Swizzle axis order (e.g. YXZ, ZYX). Empty string for no swizzle.
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "mapping_context_path": mapping_context_path,
                "input_action_path": input_action_path,
                "key": key,
                "negate": negate,
            }
            if swizzle:
                params["swizzle"] = swizzle
            
            logger.info(f"Adding action '{input_action_path}' to mapping context '{mapping_context_path}' with key '{key}'")
            response = unreal.send_command("add_action_to_mapping_context", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add action to mapping context response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding action to mapping context: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Project tools registered successfully") 